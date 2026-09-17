#include "renderer_sw.h"

#include <algorithm>

#include "engine/log.h"

namespace cr {

namespace {

FlatMeshData makeBox()
{
    FlatMeshData m;
    // per face (in axis order +X -X +Y -Y +Z -Z): normal n, tangent u, bitangent v with u x v = n, so the corners
    // (-1,-1) (1,-1) (1,1) (-1,1) run counter-clockwise seen from outside (the GLES makeBox)
    static const int faces[6][9] = {
        {1, 0, 0, 0, 0, -1, 0, 1, 0},  {-1, 0, 0, 0, 0, 1, 0, 1, 0}, {0, 1, 0, 1, 0, 0, 0, 0, -1},
        {0, -1, 0, 1, 0, 0, 0, 0, 1},  {0, 0, 1, 1, 0, 0, 0, 1, 0},  {0, 0, -1, -1, 0, 0, 0, 1, 0},
    };
    static const int corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    const int32_t half = 32768;
    for (int f = 0; f < 6; f++) {
        const uint16_t base = uint16_t(m.vertexCount());
        for (int c = 0; c < 4; c++)
            for (int k = 0; k < 3; k++)
                m.positions.push_back(faces[f][k] * half + faces[f][3 + k] * corner[c][0] * half +
                                      faces[f][6 + k] * corner[c][1] * half);
        m.triangles.push_back({base, uint16_t(base + 1), uint16_t(base + 2), 0, uint8_t(f)});
        m.triangles.push_back({base, uint16_t(base + 2), uint16_t(base + 3), 0, uint8_t(f)});
    }
    m.colors = {255, 255, 255};
    for (int i = 0; i < 3; i++) {
        m.aabbMin[i] = -0.5f;
        m.aabbMax[i] = 0.5f;
    }
    return m;
}

FlatMeshData makePlane()
{
    FlatMeshData m;
    const int32_t h = 32768;
    m.positions = {-h, -h, 0, h, -h, 0, h, h, 0, -h, h, 0};
    m.triangles.push_back({0, 1, 2, 0, 4});
    m.triangles.push_back({0, 2, 3, 0, 4});
    m.colors = {255, 255, 255};
    m.aabbMin[0] = m.aabbMin[1] = -0.5f;
    m.aabbMax[0] = m.aabbMax[1] = 0.5f;
    return m;
}

uint8_t unitTo8(mreal c)
{
    const int32_t v = int32_t((int64_t(c.v) * 255 + 32768) >> 16);
    return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v);
}

// colour (0..1, 16.16) * light (Q16), stored as 8 bits like gl_FragColor = uColor * vLight
uint8_t shadeUnit(mreal c, int32_t light)
{
    const int64_t v = (int64_t(c.v) * light * 255 + (int64_t(1) << 31)) >> 32;
    return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v);
}

// the exact depth gradient of every face with normal axis k (0 x, 1 y, 2 z) under screen transform t: the faces span the
// other two model axes u, v, so (dx, dy) = A (du, dv) and dz = (t2u, t2v) . (du, dv) -> dz/dx, dz/dy through A^-1
bool faceGradient(const sw::Mat34 &t, int k, sw::DepthGradient &out)
{
    const int u = k == 0 ? 1 : 0, v = k == 2 ? 1 : 2;
    const int64_t t0u = t.m[u], t0v = t.m[v], t1u = t.m[4 + u], t1v = t.m[4 + v], t2u = t.m[8 + u], t2v = t.m[8 + v];
    int64_t det = t0u * t1v - t0v * t1u;
    int64_t nx = t2u * t1v - t2v * t1u;
    int64_t ny = t0u * t2v - t0v * t2u;
    const int64_t limit = int64_t(1) << 44;
    while (nx >= limit || nx <= -limit || ny >= limit || ny <= -limit) {
        det /= 2;
        nx /= 2;
        ny /= 2;
    }
    if (det == 0) return false; // the faces are edge-on
    // world units per pixel -> depth steps (1024 per unit) in Q8
    const int64_t gx = nx * (int64_t(1) << (sw::kDepthUnitBits + 8)) / det;
    const int64_t gy = ny * (int64_t(1) << (sw::kDepthUnitBits + 8)) / det;
    const int64_t maxG = int64_t(1) << 23;
    if (gx > maxG || gx < -maxG || gy > maxG || gy < -maxG) return false;
    out.dzdx = int32_t(gx);
    out.dzdy = int32_t(gy);
    return true;
}

// the GLES shadow sits 0.003 above its receiver to win a 24-bit depth test; the 16-bit buffer steps 1/1024 unit, so
// the shadow also passes where it is up to this many steps behind the stored depth
constexpr int32_t kShadowDepthBias = 8;

// adds the time of its scope to `total` when a profile clock is set
struct ProfileScope {
    uint64_t (*clock)();
    int64_t &total;
    uint64_t start;
    ProfileScope(uint64_t (*c)(), int64_t &t) : clock(c), total(t), start(c ? c() : 0) {}
    ~ProfileScope()
    {
        if (clock) total += int64_t(clock() - start);
    }
};

// the rasteriser's small-triangle path needs a depth gradient even when nothing reads depth
const sw::DepthGradient kNoDepthGradient{0, 0};

struct ShadowSpan {
    const sw::RasterTarget &t;
    uint8_t *mask; // aligned with t.color
    const uint8_t *lutR, *lutG, *lutB;
    bool depth; // false: no depth buffer, the shadows go onto the floors before anything stands on them
    void operator()(int y, int x0, int x1, int32_t z, int32_t dz)
    {
        if (t.counters) t.counters->spans++, t.counters->pixels += x1 - x0;
        const int stride = t.rowStride();
        uint16_t *p = t.color + y * stride;
        const uint16_t *d = t.depth + y * stride;
        uint8_t *m = mask + y * stride;
        for (int x = x0; x < x1; x++, z += dz) {
            if (m[x] || (depth && (z >> 8) - kShadowDepthBias > int32_t(d[x]))) continue;
            m[x] = 1;
            const uint16_t c = p[x];
            p[x] = uint16_t(lutR[c >> 11] << 11 | lutG[(c >> 5) & 63] << 5 | lutB[c & 31]);
        }
    }
};

// colour, and the pixel marked in the shadow mask (Renderer::shadowProof)
struct ProofSpan {
    const sw::RasterTarget &t;
    uint8_t *mask; // aligned with t.color
    uint16_t color;
    void operator()(int y, int x0, int x1, int32_t, int32_t)
    {
        if (t.counters) t.counters->spans++, t.counters->pixels += x1 - x0;
        const int stride = t.rowStride();
        uint16_t *p = t.color + y * stride;
        uint8_t *m = mask + y * stride;
        for (int x = x0; x < x1; x++) {
            p[x] = color;
            m[x] = 1;
        }
    }
};

} // namespace

void Renderer::clearShadowMask()
{
    shadowMask_.assign(target_->color.size(), 0);
}

void Renderer::beginShadows(mreal factor)
{
    // without a depth buffer the scene cleared the mask before the floors (shadow-proof meshes may have marked it)
    if (depthBuffer || shadowMask_.size() != target_->color.size()) shadowMask_.assign(target_->color.size(), 0);
    // GLES: dst8 = round(dst8 * factor) on an RGBA8 target; here per 5/6-bit channel through the widened 8-bit value
    for (int v = 0; v < 32; v++) {
        const int32_t c8 = v << 3 | v >> 2;
        const int32_t o8 = int32_t((int64_t(c8) * factor.v + 32768) >> 16);
        shadowR_[v] = shadowB_[v] = uint8_t((o8 > 255 ? 255 : o8) >> 3);
    }
    for (int v = 0; v < 64; v++) {
        const int32_t c8 = v << 2 | v >> 4;
        const int32_t o8 = int32_t((int64_t(c8) * factor.v + 32768) >> 16);
        shadowG_[v] = uint8_t((o8 > 255 ? 255 : o8) >> 2);
    }
    // every shadow lies on a horizontal plane: the depth gradient of world y faces under the camera
    shadowGradientOk_ = faceGradient(screen(), 1, shadowGradient_);
}

void Renderer::drawShadow(const GpuMesh &mesh, const Mat4 &model, real planeY)
{
    if (!mesh.data || shadowMask_.size() != target_->color.size()) return;
    const FlatMeshData &d = *mesh.data;
    stats.drawCalls++;
    stats.triangles += int(d.triangles.size());

    // P' = P - L * (P.y - h) / L.y
    const ProfileScope profile(profileClock, stats.usShadows);
    const real h = planeY + real(0.003);
    const real kx = light_.x / light_.y, kz = light_.z / light_.y;
    Mat4 flatten = Mat4::identity();
    flatten.e[4] = -kx; // x' = x - kx * (y - h)
    flatten.e[12] = kx * h;
    flatten.e[5] = 0; // y' = h
    flatten.e[13] = h;
    flatten.e[6] = -kz; // z' = z - kz * (y - h)
    flatten.e[14] = kz * h;
    const sw::Mat34 t = sw::modelToScreen(screen(), mulAffine(flatten, model));
    beginVertices(d.vertexCount());

    const size_t offset = size_t(raster_.color - target_->color.data());
    ShadowSpan span{raster_, shadowMask_.data() + offset, shadowR_, shadowG_, shadowB_, depthBuffer};
    // A ray from the receiver towards the light leaves a closed mesh through a face turned to the light, so those faces
    // alone cover the whole shadow; the faces turned away (and those parallel to the light) are skipped.
    bool litAxis[6];
    for (int k = 0; k < 3; k++) {
        const int64_t dot = int64_t(model.e[k * 4].v) * light_.x.v + int64_t(model.e[k * 4 + 1].v) * light_.y.v +
                            int64_t(model.e[k * 4 + 2].v) * light_.z.v;
        litAxis[k * 2] = dot > 0;
        litAxis[k * 2 + 1] = dot < 0;
    }
    const bool ranges = mesh.axisStart[6] >= 0;
    for (int group = 0; group < (ranges ? 6 : 1); group++) {
        if (ranges && !litAxis[group]) continue;
        const size_t first = ranges ? size_t(mesh.axisStart[group]) : 0;
        const size_t last = ranges ? size_t(mesh.axisStart[group + 1]) : d.triangles.size();
        for (size_t i = first; i < last; i++) {
            const FlatMeshData::Triangle &tri = d.triangles[i];
            if (!litAxis[tri.axis]) continue;
            // flattened geometry has arbitrary winding
            const int32_t *pos = d.positions.data();
            sw::rasterTriangle(raster_, vertex(t, pos, tri.a), vertex(t, pos, tri.b), vertex(t, pos, tri.c), false, span,
                               shadowGradientOk_ ? &shadowGradient_ : depthBuffer ? nullptr : &kNoDepthGradient);
        }
    }
    stats.spans = counters_.spans;
    stats.pixels = counters_.pixels;
}

void Renderer::endShadows() {}

void Renderer::beginVertices(int count)
{
    verts_.resize(size_t(count));
    stamps_.resize(size_t(count), 0);
    if (++stamp_ == 0) { // wrapped after 2^32 draws: forget every stamp
        std::fill(stamps_.begin(), stamps_.end(), 0u);
        stamp_ = 1;
    }
}

GpuTexture Renderer::uploadTexture(const TextureData &tex)
{
    Texture t;
    t.width = tex.width;
    t.height = tex.height;
    t.pixels.resize(size_t(tex.width) * size_t(tex.height) * 4, 255);
    for (size_t i = 0, n = size_t(tex.width) * size_t(tex.height); i < n; i++)
        for (int c = 0; c < tex.channels && c < 4; c++) t.pixels[i * 4 + size_t(c)] = tex.pixels[i * size_t(tex.channels) + size_t(c)];
    textures_.push_back(std::move(t));
    GpuTexture g;
    g.id = int(textures_.size());
    g.width = tex.width;
    g.height = tex.height;
    return g;
}

GpuTexture Renderer::uploadAlphaTexture(int width, int height, const uint8_t *coverage)
{
    Texture t;
    t.width = width;
    t.height = height;
    t.alphaOnly = true;
    t.pixels.assign(coverage, coverage + size_t(width) * size_t(height));
    textures_.push_back(std::move(t));
    GpuTexture g;
    g.id = int(textures_.size());
    g.width = width;
    g.height = height;
    return g;
}

void Renderer::beginOverlay(int screenW, int screenH)
{
    overlayScaleX_ = screenW > 0 ? int32_t((int64_t(vpW_) << 16) / screenW) : 65536;
    overlayScaleY_ = screenH > 0 ? int32_t((int64_t(vpH_) << 16) / screenH) : 65536;
}

void Renderer::endOverlay() {}

namespace {

// src over dst in 8 bits per channel (the GLES RGBA8 blend), stored as RGB565
// x / 255 without a division (a hardware divide per blended pixel was most of the UI's ~4-10 ms on the console);
// exact for 0 <= x <= 65152 (checked exhaustively), which covers c * a + d * (255 - a) + 127 for 8-bit c, d, a
inline int div255(int x) { return ((x + 1) * 257) >> 16; }

inline uint16_t blend565(uint16_t dst, int r, int g, int b, int a)
{
    if (a >= 255) return sw::rgb565(uint8_t(r), uint8_t(g), uint8_t(b));
    const unsigned dr5 = dst >> 11, dg6 = (dst >> 5) & 63, db5 = dst & 31;
    const int dr = int(dr5 << 3 | dr5 >> 2), dg = int(dg6 << 2 | dg6 >> 4), db = int(db5 << 3 | db5 >> 2);
    const int ia = 255 - a;
    return sw::rgb565(uint8_t(div255(r * a + dr * ia + 127)), uint8_t(div255(g * a + dg * ia + 127)),
                      uint8_t(div255(b * a + db * ia + 127)));
}

} // namespace

void Renderer::overlayQuad(const Texture *tex, mreal x0, mreal y0, mreal x1, mreal y1, mreal u0, mreal v0, mreal u1,
                           mreal v1, int r, int g, int b, int a, bool imageColors)
{
    const ProfileScope profile(profileClock, stats.usOverlay);
    if (a <= 0) return;
    if (overlayScaleX_ != 65536) {
        x0 = mreal::fromRaw(int32_t((int64_t(x0.v) * overlayScaleX_) >> 16));
        x1 = mreal::fromRaw(int32_t((int64_t(x1.v) * overlayScaleX_) >> 16));
    }
    if (overlayScaleY_ != 65536) {
        y0 = mreal::fromRaw(int32_t((int64_t(y0.v) * overlayScaleY_) >> 16));
        y1 = mreal::fromRaw(int32_t((int64_t(y1.v) * overlayScaleY_) >> 16));
    }
    if (x1 < x0) std::swap(x0, x1), std::swap(u0, u1);
    if (y1 < y0) std::swap(y0, y1), std::swap(v0, v1);
    if (x1.v - x0.v <= 0 || y1.v - y0.v <= 0) return;
    // the first pixel whose centre is at or past the edge: ceil(x - 0.5)
    auto firstPixel = [](mreal e) { return int((int64_t(e.v) - 32768 + 65535) >> 16); };
    const int px0 = std::max(0, firstPixel(x0)), px1 = std::min(raster_.width, firstPixel(x1));
    const int py0 = std::max(0, firstPixel(y0)), py1 = std::min(raster_.height, firstPixel(y1));
    if (px0 >= px1 || py0 >= py1) return;
    const int stride = raster_.rowStride();
    // O7.7: while capturing, the writes go into the capture (beginOverlayCapture)
    uint16_t *const colorBase = capturing_ ? captureColor_.data() : raster_.color;
    uint8_t *const maskBase = capturing_ ? captureMask_.data() : nullptr;
    if (capturing_) {
        captureX0_ = std::min(captureX0_, px0);
        captureX1_ = std::max(captureX1_, px1);
        captureY0_ = std::min(captureY0_, py0);
        captureY1_ = std::max(captureY1_, py1);
    }
    if (!tex) {
        const uint16_t solid = sw::rgb565(uint8_t(r), uint8_t(g), uint8_t(b));
        if (capturing_ && a < 255) captureExact_ = false;
        for (int y = py0; y < py1; y++) {
            uint16_t *row = colorBase + y * stride;
            uint8_t *const mrow = maskBase ? maskBase + y * stride : nullptr;
            for (int x = px0; x < px1; x++) {
                row[x] = a >= 255 ? solid : blend565(row[x], r, g, b, a);
                if (mrow) mrow[x] = 1;
            }
        }
        return;
    }
    // texel coordinates (Q16) at pixel centres: t = t0 + (t1 - t0) * (centre - x0) / (x1 - x0)
    const int64_t s0 = int64_t(u0.v) * tex->width, s1 = int64_t(u1.v) * tex->width;
    const int64_t t0 = int64_t(v0.v) * tex->height, t1 = int64_t(v1.v) * tex->height;
    const int64_t ds = ((s1 - s0) * 65536) / (int64_t(x1.v) - x0.v); // Q16 texels per pixel
    const int64_t dt = ((t1 - t0) * 65536) / (int64_t(y1.v) - y0.v);
    const int64_t sStart = s0 + ((ds * ((int64_t(px0) << 16) + 32768 - x0.v)) >> 16);
    const int bytes = tex->alphaOnly ? 1 : 4;
    for (int y = py0; y < py1; y++) {
        int ty = int((t0 + ((dt * ((int64_t(y) << 16) + 32768 - y0.v)) >> 16)) >> 16);
        ty = ty < 0 ? 0 : ty >= tex->height ? tex->height - 1 : ty;
        const uint8_t *texRow =tex->pixels.data() + size_t(ty) * size_t(tex->width) * size_t(bytes);
        uint16_t *row = colorBase + y * stride;
        uint8_t *const mrow = maskBase ? maskBase + y * stride : nullptr;
        int64_t s = sStart;
        for (int x = px0; x < px1; x++, s += ds) {
            int tx = int(s >> 16);
            tx = tx < 0 ? 0 : tx >= tex->width ? tex->width - 1 : tx;
            const uint8_t *texel =texRow + tx * bytes;
            int sr = r, sg = g, sb = b, sa;
            if (tex->alphaOnly) {
                sa = texel[0];
            } else {
                sa = texel[3];
                if (imageColors) {
                    sr = div255(texel[0] * r + 127);
                    sg = div255(texel[1] * g + 127);
                    sb = div255(texel[2] * b + 127);
                }
            }
            if (a < 255) sa = div255(sa * a + 127);
            if (sa == 0) continue;
            if (mrow) {
                if (sa != 255) captureExact_ = false; // a blend needs the target's pixel
                mrow[x] = 1;
            }
            row[x] = blend565(row[x], sr, sg, sb, sa);
        }
    }
}

void Renderer::beginOverlayCapture()
{
    const size_t n = size_t(raster_.rowStride()) * size_t(raster_.height) + size_t(raster_.width);
    if (captureColor_.size() != n) {
        captureColor_.assign(n, 0);
        captureMask_.assign(n, 0);
    }
    capturing_ = true;
    captureExact_ = true;
    captureX0_ = captureY0_ = 1 << 30;
    captureX1_ = captureY1_ = -1;
}

void Renderer::endOverlayCapture(OverlayCapture &out)
{
    capturing_ = false;
    out.valid = captureExact_;
    const int x0 = captureX0_, y0 = captureY0_, x1 = captureX1_, y1 = captureY1_;
    if (x1 <= x0 || y1 <= y0) {
        out.x = out.y = out.w = out.h = 0;
        out.color.clear();
        out.mask.clear();
        return;
    }
    const int stride = raster_.rowStride();
    out.x = x0;
    out.y = y0;
    out.w = x1 - x0;
    out.h = y1 - y0;
    out.color.resize(size_t(out.w) * size_t(out.h));
    out.mask.resize(size_t(out.w) * size_t(out.h));
    for (int y = 0; y < out.h; y++) {
        const uint16_t *c = captureColor_.data() + (y0 + y) * stride + x0;
        uint8_t *m = captureMask_.data() + (y0 + y) * stride + x0;
        std::copy(c, c + out.w, out.color.begin() + y * out.w);
        std::copy(m, m + out.w, out.mask.begin() + y * out.w);
        std::fill(m, m + out.w, uint8_t(0)); // the next capture starts without marks
    }
}

void Renderer::drawOverlayCapture(const OverlayCapture &capture)
{
    const ProfileScope profile(profileClock, stats.usOverlay);
    const int stride = raster_.rowStride();
    for (int y = 0; y < capture.h; y++) {
        uint16_t *row = raster_.color + (capture.y + y) * stride + capture.x;
        const uint16_t *src = capture.color.data() + y * capture.w;
        const uint8_t *m = capture.mask.data() + y * capture.w;
        for (int x = 0; x < capture.w; x++)
            if (m[x]) row[x] = src[x];
    }
}

void Renderer::drawOverlayTriangles(const GpuTexture &tex, const std::vector<mreal> &xyuv, mreal r, mreal g, mreal b,
                                    mreal a, bool imageColors)
{
    if (xyuv.size() < 12) return;
    const Texture *t = tex.id > 0 && size_t(tex.id) <= textures_.size() ? &textures_[size_t(tex.id - 1)] : nullptr;
    const int r8 = unitTo8(r), g8 = unitTo8(g), b8 = unitTo8(b), a8 = unitTo8(a);
    stats.drawCalls++;
    stats.triangles += int(xyuv.size() / 12);
    for (size_t q = 0; q + 24 <= xyuv.size(); q += 24) {
        const mreal *v = &xyuv[q];
        // (x0 y0) (x1 y0) (x1 y1) (x0 y0) (x1 y1) (x0 y1), as text.cpp and quad() build them
        const bool quad = v[5] == v[1] && v[4] == v[8] && v[12] == v[0] && v[13] == v[1] && v[16] == v[8] &&
                          v[17] == v[9] && v[20] == v[0] && v[21] == v[9];
        if (!quad) {
            if (!warnedNonQuad_) logf("renderer: overlay triangles that are not axis-aligned quads are skipped");
            warnedNonQuad_ = true;
            continue;
        }
        overlayQuad(t, v[0], v[1], v[8], v[9], v[2], v[3], v[10], v[11], r8, g8, b8, a8, imageColors);
    }
}

void Renderer::drawOverlayImage(const GpuTexture &tex, mreal x, mreal y, mreal w, mreal h, mreal alpha)
{
    const Texture *t = tex.id > 0 && size_t(tex.id) <= textures_.size() ? &textures_[size_t(tex.id - 1)] : nullptr;
    stats.drawCalls++;
    stats.triangles += 2;
    if (t) overlayQuad(t, x, y, x + w, y + h, 0, 0, 1, 1, 255, 255, 255, unitTo8(alpha), true);
}

void Renderer::drawOverlayRect(mreal x, mreal y, mreal w, mreal h, mreal r, mreal g, mreal b, mreal a)
{
    stats.drawCalls++;
    stats.triangles += 2;
    overlayQuad(nullptr, x, y, x + w, y + h, 0, 0, 1, 1, unitTo8(r), unitTo8(g), unitTo8(b), unitTo8(a), false);
}

bool Renderer::init(int width, int height)
{
    createTarget(default_, width, height);
    bindTarget(nullptr);
    unitBoxData = makeBox();
    unitPlaneData = makePlane();
    unitBox = uploadMesh(unitBoxData);
    unitPlane = uploadMesh(unitPlaneData);
    return true;
}

GpuMesh Renderer::uploadMesh(const FlatMeshData &mesh)
{
    meshes_.emplace_back(new FlatMeshData(mesh));
    GpuMesh g;
    g.data = meshes_.back().get();
    g.indexCount = int(mesh.triangles.size()) * 3;
    g.aabbMin = {realFromFloat(mesh.aabbMin[0]), realFromFloat(mesh.aabbMin[1]), realFromFloat(mesh.aabbMin[2])};
    g.aabbMax = {realFromFloat(mesh.aabbMax[0]), realFromFloat(mesh.aabbMax[1]), realFromFloat(mesh.aabbMax[2])};
    const int n = int(mesh.triangles.size());
    bool sorted = true;
    for (int i = 1; i < n; i++)
        if (mesh.triangles[size_t(i)].axis < mesh.triangles[size_t(i - 1)].axis) sorted = false;
    if (sorted) {
        int next = 0;
        for (int a = 0; a <= 6; a++) {
            while (next < n && mesh.triangles[size_t(next)].axis < a) next++;
            g.axisStart[a] = next;
        }
    }
    return g;
}

void Renderer::releaseMesh(GpuMesh &mesh)
{
    if (mesh.data) {
        auto it = std::find_if(meshes_.begin(), meshes_.end(),
                               [&](const std::unique_ptr<FlatMeshData> &p) { return p.get() == mesh.data; });
        if (it != meshes_.end()) meshes_.erase(it);
    }
    mesh = GpuMesh();
}

bool Renderer::createTarget(RenderTarget &t, int width, int height)
{
    t.width = width;
    t.height = height;
    t.color.assign(size_t(width) * size_t(height), 0);
    t.depth.assign(size_t(width) * size_t(height), uint16_t(sw::kDepthMax));
    return width > 0 && height > 0;
}

void Renderer::bindTarget(RenderTarget *t)
{
    target_ = t ? t : &default_;
    viewport(0, 0, target_->width, target_->height);
}

void Renderer::viewport(int x, int y, int w, int h, bool)
{
    // GL clips primitives to the viewport, scissor or not
    const int W = target_->width, H = target_->height;
    int left = std::max(0, x), top = std::max(0, H - (y + h));
    int right = std::min(W, x + w), bottom = std::min(H, H - y);
    if (right < left) right = left;
    if (bottom < top) bottom = top;
    raster_.color = target_->color.data() + top * W + left;
    raster_.depth = target_->depth.data() + top * W + left;
    raster_.width = right - left;
    raster_.height = bottom - top;
    raster_.stride = W;
    raster_.counters = &counters_;
    vpW_ = w;
    vpH_ = h;
    screenDirty_ = true;
}

void Renderer::clear(mreal r, mreal g, mreal b)
{
    sw::RasterTarget target = raster_;
    if (!depthBuffer) target.depth = nullptr;
    sw::clearTarget(target, sw::rgb565(unitTo8(r), unitTo8(g), unitTo8(b)));
}

void Renderer::drawClippedBelow(const sw::Mat34 &t, const int32_t *positions, const FlatMeshData::Triangle &tri,
                                const int32_t worldY[3], int32_t clip, uint16_t shade)
{
    struct Point {
        int64_t x, y, z; // model space, 16.16
        int32_t wy;      // world height, Q16
    };
    const uint16_t idx[3] = {tri.a, tri.b, tri.c};
    Point in[3];
    for (int k = 0; k < 3; k++) {
        const int32_t *q = positions + idx[k] * 3;
        in[k] = {q[0], q[1], q[2], worldY[k]};
    }
    // Sutherland-Hodgman against wy >= clip: one plane turns a triangle into at most four points
    Point out[4];
    int n = 0;
    for (int k = 0; k < 3; k++) {
        const Point &p = in[k], &q = in[(k + 1) % 3];
        const bool pIn = p.wy >= clip, qIn = q.wy >= clip;
        if (pIn) out[n++] = p;
        if (pIn != qIn) {
            const int64_t num = int64_t(clip) - p.wy, den = int64_t(q.wy) - p.wy;
            out[n++] = {p.x + (q.x - p.x) * num / den, p.y + (q.y - p.y) * num / den, p.z + (q.z - p.z) * num / den, clip};
        }
    }
    if (n < 3) return;
    sw::ScreenVertex v[4];
    for (int k = 0; k < n; k++)
        v[k] = sw::toScreen(sw::transformQ32(t, int32_t(out[k].x), int32_t(out[k].y), int32_t(out[k].z)));
    const bool proof = shadowProof && shadowMask_.size() == target_->color.size();
    for (int k = 1; k + 1 < n; k++) {
        if (proof) {
            ProofSpan span{raster_, shadowMask_.data() + size_t(raster_.color - target_->color.data()), shade};
            sw::rasterTriangle(raster_, v[0], v[k], v[k + 1], false, span, &kNoDepthGradient);
        } else {
            sw::drawTriangle(raster_, v[0], v[k], v[k + 1], shade, 0, &kNoDepthGradient);
        }
    }
}

const std::vector<uint16_t> &Renderer::paintOrder(const GpuMesh &mesh, const sw::Mat34 &t)
{
    // the camera's depth direction in world units is the screen matrix's depth row; turned by 90 degree steps about Y
    // it gives the mesh-space direction of each turn: R_y(a)^T f
    const sw::Mat34 &s = screen();
    const int64_t fx = s.m[8], fy = s.m[9], fz = s.m[10];
    const int64_t dirX[4] = {fx, -fz, -fx, fz}, dirZ[4] = {fz, fx, -fz, -fx};
    const bool known = mesh.paintOrder >= 0;
    if (!known) {
        mesh.paintOrder = int(paintOrders_.size());
        paintOrders_.emplace_back();
    }
    PaintOrder &o = paintOrders_[size_t(mesh.paintOrder)];
    if (!known || o.view[0] != s.m[8] || o.view[1] != s.m[9] || o.view[2] != s.m[10] || o.key != sw::gPaintKey) {
        o.view[0] = s.m[8], o.view[1] = s.m[9], o.view[2] = s.m[10];
        o.key = sw::gPaintKey;
        const FlatMeshData &d = *mesh.data;
        const size_t n = d.triangles.size();
        const int32_t *p = d.positions.data();
        std::vector<std::pair<int64_t, uint16_t>> keys(n);
        for (int turn = 0; turn < 4; turn++) {
            for (size_t i = 0; i < n; i++) {
                const FlatMeshData::Triangle &tri = d.triangles[i];
                // depth along the direction (16.16 positions, larger = farther): three times the centroid's, or the
                // nearest / farthest vertex's (a long merged face - a rail - must not sort by its middle)
                const auto depthOf = [&](uint16_t v) {
                    return (int64_t(p[v * 3]) * dirX[turn] + int64_t(p[v * 3 + 1]) * fy + int64_t(p[v * 3 + 2]) * dirZ[turn]) >> 16;
                };
                const int64_t da = depthOf(tri.a), db = depthOf(tri.b), dc = depthOf(tri.c);
                const int64_t key = sw::gPaintKey == 1   ? std::min(da, std::min(db, dc)) * 3
                                    : sw::gPaintKey == 2 ? std::max(da, std::max(db, dc)) * 3
                                                         : da + db + dc;
                keys[i] = {key, uint16_t(i)};
            }
            std::stable_sort(keys.begin(), keys.end(),
                             [](const std::pair<int64_t, uint16_t> &a, const std::pair<int64_t, uint16_t> &b) {
                                 return a.first > b.first;
                             });
            o.turn[turn].resize(n);
            for (size_t i = 0; i < n; i++) o.turn[turn][i] = keys[i].second;
        }
    }
    // the draw's mesh-space depth direction is t's depth row: the turn pointing the same way
    int best = 0;
    int64_t bestDot = 0;
    for (int turn = 0; turn < 4; turn++) {
        const int64_t dot = ((int64_t(t.m[8]) * dirX[turn]) >> 16) + ((int64_t(t.m[9]) * fy) >> 16) +
                            ((int64_t(t.m[10]) * dirZ[turn]) >> 16);
        if (turn == 0 || dot > bestDot) best = turn, bestDot = dot;
    }
    return o.turn[best];
}

bool Renderer::readPixels(int w, int h, std::vector<uint8_t> &out)
{
    const RenderTarget &t = *target_;
    if (w > t.width || h > t.height) return false;
    out.resize(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint16_t p = t.color[size_t(y * t.width + x)];
            const unsigned r = p >> 11, g = (p >> 5) & 63, b = p & 31;
            uint8_t *o = &out[size_t(y * w + x) * 4];
            o[0] = uint8_t(r << 3 | r >> 2);
            o[1] = uint8_t(g << 2 | g >> 4);
            o[2] = uint8_t(b << 3 | b >> 2);
            o[3] = 255;
        }
    return true;
}

void Renderer::setCamera(const Mat4 &projection, const Mat4 &view)
{
    projection_ = projection;
    view_ = view;
    screenDirty_ = true;
}

const sw::Mat34 &Renderer::screen()
{
    if (screenDirty_) {
        screen_ = sw::screenMatrix(projection_, view_, vpW_, vpH_);
        screenDirty_ = false;
    }
    return screen_;
}

void Renderer::setLightDirection(const Vec3 &towardsLight)
{
    light_ = normalize(towardsLight);
}

void Renderer::drawLambert(const GpuMesh &mesh, const GpuTexture &, const Mat4 &model)
{
    drawMesh(mesh, model, nullptr, false);
}

void Renderer::drawFlat(const GpuMesh &mesh, const Vec3 &color, const Mat4 &model, bool doubleSided)
{
    drawMesh(mesh, model, &color, doubleSided);
}

void Renderer::drawMesh(const GpuMesh &mesh, const Mat4 &model, const Vec3 *color, bool doubleSided)
{
    if (!mesh.data) return;
    const FlatMeshData &d = *mesh.data;
    stats.drawCalls++;
    stats.triangles += int(d.triangles.size());

    const uint64_t t0 = profileClock ? profileClock() : 0;
    const sw::Mat34 t = sw::modelToScreen(screen(), model);
    beginVertices(d.vertexCount());

    int32_t light[6];
    sw::axisBrightness(model, light_, light);
    const int colors = color ? 1 : d.colorCount();
    shades_.resize(size_t(colors) * 6);
    for (int c = 0; c < colors; c++)
        for (int axis = 0; axis < 6; axis++) {
            uint8_t r, g, b;
            if (color) {
                r = shadeUnit(color->x, light[axis]);
                g = shadeUnit(color->y, light[axis]);
                b = shadeUnit(color->z, light[axis]);
            } else {
                const uint8_t *rgb = &d.colors[size_t(c) * 3];
                r = sw::shade8(rgb[0], light[axis]);
                g = sw::shade8(rgb[1], light[axis]);
                b = sw::shade8(rgb[2], light[axis]);
            }
            shades_[size_t(c * 6 + axis)] = sw::rgb565(r, g, b);
        }

    sw::DepthGradient gradients[3];
    bool gradientOk[3] = {false, false, false};
    if (depthBuffer)
        for (int k = 0; k < 3; k++) gradientOk[k] = faceGradient(t, k, gradients[k]);
    const uint64_t t1 = profileClock ? profileClock() : 0;

    // An orthographic camera sees every face of one axis from the same side, so whole axes facing away are skipped.
    // The screen area of the model triangle (0, e_u, e_v), with u, v the other two axes in right-handed order, has the
    // sign of the +axis faces' area: negative (clockwise on the y-down screen) = front.
    bool frontAxis[6];
    for (int k = 0; k < 3; k++) {
        const int u = k == 0 ? 1 : 0, v = k == 2 ? 1 : 2;
        const int64_t det = int64_t(t.m[u]) * t.m[4 + v] - int64_t(t.m[v]) * t.m[4 + u];
        const int64_t areaPlus = k == 1 ? -det : det; // (x, z) is left-handed for the y axis: z x x = +y
        frontAxis[k * 2] = areaPlus < 0;
        frontAxis[k * 2 + 1] = areaPlus > 0;
    }
    if (!depthBuffer) {
        // painter's order: the mesh's front faces far to near (paintOrder), colour only
        const int32_t *pos = d.positions.data();
        const int32_t clip = clipBelowY.v;
        // world height of a vertex (Q16, relative to the camera origin like the matrix)
        const auto worldY = [&](uint16_t v) {
            const int32_t *q = pos + v * 3;
            return int32_t(((int64_t(model.e[1].v) * q[0] + int64_t(model.e[5].v) * q[1] + int64_t(model.e[9].v) * q[2]) >>
                            16) +
                           model.e[13].v);
        };
        for (uint16_t i : paintOrder(mesh, t)) {
            const FlatMeshData::Triangle &tri = d.triangles[i];
            if (!doubleSided && !frontAxis[tri.axis]) continue;
            int32_t ys[3] = {0, 0, 0};
            int below = 0;
            if (clipBelow) {
                ys[0] = worldY(tri.a), ys[1] = worldY(tri.b), ys[2] = worldY(tri.c);
                below = (ys[0] < clip) + (ys[1] < clip) + (ys[2] < clip);
                if (below == 3) continue;
            }
            const sw::ScreenVertex &a = vertex(t, pos, tri.a), &b = vertex(t, pos, tri.b), &c = vertex(t, pos, tri.c);
            const int64_t area = int64_t(b.x - a.x) * (c.y - a.y) - int64_t(c.x - a.x) * (b.y - a.y);
            if (area == 0) continue;
            const bool front = area < 0;
            if (!front && !doubleSided) continue;
            const int axis = front ? tri.axis : tri.axis ^ 1;
            const uint16_t shade = shades_[size_t((color ? 0 : tri.color) * 6 + axis)];
            if (below > 0) {
                drawClippedBelow(t, pos, tri, ys, clip, shade);
            } else if (shadowProof && shadowMask_.size() == target_->color.size()) {
                ProofSpan span{raster_, shadowMask_.data() + size_t(raster_.color - target_->color.data()), shade};
                sw::rasterTriangle(raster_, a, b, c, false, span, &kNoDepthGradient);
            } else {
                sw::drawTriangle(raster_, a, b, c, shade, 0, &kNoDepthGradient);
            }
            stats.trianglesDrawn++;
        }
    } else {
    const bool ranges = mesh.axisStart[6] >= 0;
    for (int group = 0; group < (ranges ? 6 : 1); group++) {
        if (ranges && !doubleSided && !frontAxis[group]) continue;
        const size_t first = ranges ? size_t(mesh.axisStart[group]) : 0;
        const size_t last = ranges ? size_t(mesh.axisStart[group + 1]) : d.triangles.size();
        for (size_t i = first; i < last; i++) {
            const FlatMeshData::Triangle &tri = d.triangles[i];
            if (!doubleSided && !frontAxis[tri.axis]) continue;
            const int32_t *pos = d.positions.data();
            const sw::ScreenVertex &a = vertex(t, pos, tri.a), &b = vertex(t, pos, tri.b), &c = vertex(t, pos, tri.c);
            const int64_t area = int64_t(b.x - a.x) * (c.y - a.y) - int64_t(c.x - a.x) * (b.y - a.y);
            if (area == 0) continue;
            const bool front = area < 0; // counter-clockwise in GL = clockwise on the y-down screen
            if (!front && !doubleSided) continue;
            const int axis = front ? tri.axis : tri.axis ^ 1; // back faces are lit with -N
            const uint16_t shade = shades_[size_t((color ? 0 : tri.color) * 6 + axis)];
            const int k = tri.axis >> 1;
            sw::drawTriangle(raster_, a, b, c, shade, sw::kDepthTest | sw::kDepthWrite,
                             gradientOk[k] ? &gradients[k] : nullptr);
            stats.trianglesDrawn++;
        }
    }
    }
    if (profileClock) {
        const uint64_t t2 = profileClock();
        stats.usTransform += int64_t(t1 - t0);
        stats.usRaster += int64_t(t2 - t1);
    }
    stats.spans = counters_.spans;
    stats.pixels = counters_.pixels;
}

} // namespace cr
