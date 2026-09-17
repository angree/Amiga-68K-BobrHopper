// Triangle rasteriser of the SF2000 software renderer (task R1.3): integer only, 28.4 subpixel vertices.
//
// Coverage: a pixel belongs to a triangle when its centre is inside, with the top-left rule for centres exactly on an
// edge (left and top edges in, right and bottom edges out), so triangles sharing an edge never leave a gap or draw a
// pixel twice. Rows are walked with an exact integer DDA (one 32-bit division per edge), which needs coordinates within
// the guard band (+-kGuard subpixels); larger triangles are clipped to it first, at points computed the same way for
// both triangles of a shared edge.
// Depth: the plane through the three vertices in Q8 depth steps, compared "less or equal" like the GLES build.
//
// rasterTriangle calls span(y, x0, x1, zQ8, dzdxQ8) for every row with pixels x0 .. x1-1 (already inside the target);
// zQ8 is the depth at the centre of x0.
#pragma once

#include <cstdint>
#include <utility>

#include "sw_math.h"

namespace cr {
namespace sw {

constexpr int32_t kGuard = 1024 * kSubpixelOne; // +-1024 pixels

// work counters (optional): rows of pixels handed to span functions and the pixels they covered
struct RasterCounters {
    int spans = 0;
    int64_t pixels = 0;
};

// a rectangle of pixels to draw into: `color`/`depth` point at its top-left pixel, rows are `stride` apart (a viewport
// inside a larger framebuffer); stride 0 means width
struct RasterTarget {
    int width = 0, height = 0;
    int stride = 0;
    uint16_t *color = nullptr; // RGB565, rows top first
    uint16_t *depth = nullptr; // kDepthMax = far
    RasterCounters *counters = nullptr;
    int rowStride() const { return stride ? stride : width; }
};

// depth change per pixel in Q8 steps. Computed from three rounded vertex depths, a sliver's gradient can be far off
// (tests/test_sw_raster.cpp); the renderer knows each face's plane exactly (every face of one axis in a node shares it)
// and passes it instead.
struct DepthGradient {
    int32_t dzdx, dzdy;
};

namespace detail {

inline int32_t floorDiv(int32_t n, int32_t d) { return n >= 0 ? n / d : -((-n + d - 1) / d); } // d > 0
inline int32_t ceilDiv(int32_t n, int32_t d) { return -floorDiv(-n, d); }

// x of the first pixel centre at or right of the edge, per row; exact rational arithmetic
struct EdgeWalk {
    int32_t x, e, sq, sr, d;
    void init(const ScreenVertex &p, const ScreenVertex &q, int row) // q.y > p.y
    {
        const int32_t dy = q.y - p.y, dx = q.x - p.x;
        d = dy * kSubpixelOne;
        const int32_t yc = row * kSubpixelOne + kSubpixelOne / 2;
        // centre index i with 16 i + 8 >= x(yc): ceil(((p.x - 8) dy + dx (yc - p.y)) / (16 dy))
        const int32_t n = (p.x - kSubpixelOne / 2) * dy + dx * (yc - p.y);
        x = ceilDiv(n, d);
        e = x * d - n;
        const int32_t s = dx * kSubpixelOne;
        sq = floorDiv(s, d);
        sr = s - sq * d;
    }
    void step()
    {
        x += sq;
        e -= sr;
        if (e < 0) {
            x++;
            e += d;
        }
    }
};

inline int64_t roundDiv(int64_t n, int64_t d)
{
    if (d < 0) n = -n, d = -d;
    return n >= 0 ? (n + d / 2) / d : -((-n + d / 2) / d);
}

// coordinates within the guard band; the caller has rejected zero-area and culled triangles
template <class Span>
void rasterGuarded(const RasterTarget &t, ScreenVertex v0, ScreenVertex v1, ScreenVertex v2, Span &span,
                   const DepthGradient *gradient)
{
    if (v1.y < v0.y) std::swap(v0, v1);
    if (v2.y < v0.y) std::swap(v0, v2);
    if (v2.y < v1.y) std::swap(v1, v2);
    const int32_t dx1 = v1.x - v0.x, dy1 = v1.y - v0.y, dx2 = v2.x - v0.x, dy2 = v2.y - v0.y;
    const int64_t area = int64_t(dx1) * dy2 - int64_t(dx2) * dy1;
    if (area == 0) return;

    const int half = kSubpixelOne / 2;
    int j0 = ceilDiv(v0.y - half, kSubpixelOne), jm = ceilDiv(v1.y - half, kSubpixelOne),
        j1 = ceilDiv(v2.y - half, kSubpixelOne);
    if (j0 < 0) j0 = 0;
    if (jm < j0) jm = j0;
    if (j1 > t.height) j1 = t.height;
    if (jm > j1) jm = j1;
    if (j0 >= j1) return;

    // depth plane in Q8 steps per pixel
    int64_t gx, gy;
    if (gradient) {
        gx = gradient->dzdx;
        gy = gradient->dzdy;
    } else {
        const int32_t dz1 = v1.z - v0.z, dz2 = v2.z - v0.z;
        gx = ((int64_t(dz1) * dy2 - int64_t(dz2) * dy1) * (kSubpixelOne << 8)) / area;
        gy = ((int64_t(dz2) * dx1 - int64_t(dz1) * dx2) * (kSubpixelOne << 8)) / area;
    }
    const int64_t kMaxGradient = int64_t(1) << 23; // only slivers get near it; they span a pixel or two
    if (gx > kMaxGradient) gx = kMaxGradient;
    if (gx < -kMaxGradient) gx = -kMaxGradient;
    if (gy > kMaxGradient) gy = kMaxGradient;
    if (gy < -kMaxGradient) gy = -kMaxGradient;

    EdgeWalk longEdge, shortEdge;
    longEdge.init(v0, v2, j0);
    const bool longIsLeft = area > 0; // v1 lies right of v0 -> v2
    int j = j0;
    for (int half2 = 0; half2 < 2; half2++) {
        const int end = half2 == 0 ? jm : j1;
        if (j >= end) continue;
        if (half2 == 0) shortEdge.init(v0, v1, j);
        else shortEdge.init(v1, v2, j);
        EdgeWalk &left = longIsLeft ? longEdge : shortEdge;
        EdgeWalk &right = longIsLeft ? shortEdge : longEdge;
        for (; j < end; j++) {
            int x0 = left.x, x1 = right.x;
            if (x0 < 0) x0 = 0;
            if (x1 > t.width) x1 = t.width;
            if (x0 < x1) {
                const int32_t xc = x0 * kSubpixelOne + half, yc = j * kSubpixelOne + half;
                int64_t z = (int64_t(v0.z) << 8) + ((gx * (xc - v0.x) + gy * (yc - v0.y)) >> kSubpixelBits);
                const int64_t lo = -(int64_t(1) << 26), hi = int64_t(1) << 26;
                if (z < lo) z = lo;
                if (z > hi) z = hi;
                span(j, x0, x1, int32_t(z), int32_t(gx));
            }
            left.step();
            right.step();
        }
    }
}

// Triangles whose pixel bounding box is at most this many pixels (most of the scene: ~15 px on average) take the
// half-space loop: three edge functions stepped over the box, no division and no sorting. A variable so benchmarks can
// compare the paths (sw_game --small-raster 0).
inline int gSmallTrianglePixels = 256;

// the same coverage as rasterGuarded (the top-left rule as a -1 bias on edges that are not top or left); needs a depth
// gradient and a bounding box [bx0, bx1) x [by0, by1) already clipped to the target, and area != 0
template <class Span>
void rasterSmall(ScreenVertex v0, ScreenVertex v1, ScreenVertex v2, int64_t area, Span &span,
                 const DepthGradient &g, int bx0, int by0, int bx1, int by1)
{
    if (area < 0) std::swap(v1, v2); // interior on the positive side of every edge
    const ScreenVertex *e[3][2] = {{&v0, &v1}, {&v1, &v2}, {&v2, &v0}};
    const int32_t cx = bx0 * kSubpixelOne + kSubpixelOne / 2, cy = by0 * kSubpixelOne + kSubpixelOne / 2;
    int32_t row[3], stepX[3], stepY[3];
    for (int i = 0; i < 3; i++) {
        const ScreenVertex &p = *e[i][0], &q = *e[i][1];
        const int32_t dx = q.x - p.x, dy = q.y - p.y;
        const bool topLeft = -dy > 0 || (dy == 0 && dx > 0); // interior to the right, or below a horizontal edge
        row[i] = dx * (cy - p.y) - dy * (cx - p.x) - (topLeft ? 0 : 1);
        stepX[i] = -dy * kSubpixelOne;
        stepY[i] = dx * kSubpixelOne;
    }
    int64_t z0 = (int64_t(v0.z) << 8) + ((int64_t(g.dzdx) * (cx - v0.x) + int64_t(g.dzdy) * (cy - v0.y)) >> kSubpixelBits);
    const int64_t lo = -(int64_t(1) << 26), hi = int64_t(1) << 26;
    int32_t zRow = int32_t(z0 < lo ? lo : z0 > hi ? hi : z0);
    for (int y = by0; y < by1; y++, zRow += g.dzdy) {
        int32_t w0 = row[0], w1 = row[1], w2 = row[2];
        int x = bx0;
        for (; x < bx1 && (w0 | w1 | w2) < 0; x++) w0 += stepX[0], w1 += stepX[1], w2 += stepX[2];
        const int start = x;
        for (; x < bx1 && (w0 | w1 | w2) >= 0; x++) w0 += stepX[0], w1 += stepX[1], w2 += stepX[2];
        if (x > start) span(y, start, x, zRow + g.dzdx * (start - bx0), g.dzdx);
        row[0] += stepY[0], row[1] += stepY[1], row[2] += stepY[2];
    }
}

// the point where edge p-q crosses coordinate `bound` on `axis` (0 x, 1 y); the endpoints are put in a fixed order
// first so the two triangles of a shared edge get the same point
inline ScreenVertex clipPoint(ScreenVertex p, ScreenVertex q, int axis, int32_t bound)
{
    if (q.x < p.x || (q.x == p.x && q.y < p.y)) std::swap(p, q);
    const int32_t pc = axis ? p.y : p.x, qc = axis ? q.y : q.x;
    const int64_t num = int64_t(bound) - pc, den = int64_t(qc) - pc;
    auto lerp = [&](int32_t u, int32_t v) { return int32_t(u + roundDiv((int64_t(v) - u) * num, den)); };
    ScreenVertex r;
    r.x = axis ? lerp(p.x, q.x) : bound;
    r.y = axis ? bound : lerp(p.y, q.y);
    r.z = lerp(p.z, q.z);
    return r;
}

inline bool insideBand(const ScreenVertex &v, int plane)
{
    switch (plane) {
    case 0: return v.x >= -kGuard;
    case 1: return v.x <= kGuard;
    case 2: return v.y >= -kGuard;
    default: return v.y <= kGuard;
    }
}

} // namespace detail

inline bool insideGuard(const ScreenVertex &v)
{
    return v.x >= -kGuard && v.x <= kGuard && v.y >= -kGuard && v.y <= kGuard;
}

// cullBack: skip triangles that are not counter-clockwise in GL terms (clockwise on the y-down screen)
template <class Span>
void rasterTriangle(const RasterTarget &t, const ScreenVertex &a, const ScreenVertex &b, const ScreenVertex &c,
                    bool cullBack, Span &span, const DepthGradient *gradient = nullptr)
{
    const int64_t area = int64_t(b.x - a.x) * (c.y - a.y) - int64_t(c.x - a.x) * (b.y - a.y);
    if (area == 0 || (cullBack && area > 0)) return;
    const int32_t w = t.width * kSubpixelOne, h = t.height * kSubpixelOne;
    if ((a.x < 0 && b.x < 0 && c.x < 0) || (a.x > w && b.x > w && c.x > w) || (a.y < 0 && b.y < 0 && c.y < 0) ||
        (a.y > h && b.y > h && c.y > h))
        return;
    if (insideGuard(a) && insideGuard(b) && insideGuard(c)) {
        if (gradient) {
            const int32_t minX = a.x < b.x ? (a.x < c.x ? a.x : c.x) : (b.x < c.x ? b.x : c.x);
            const int32_t maxX = a.x > b.x ? (a.x > c.x ? a.x : c.x) : (b.x > c.x ? b.x : c.x);
            const int32_t minY = a.y < b.y ? (a.y < c.y ? a.y : c.y) : (b.y < c.y ? b.y : c.y);
            const int32_t maxY = a.y > b.y ? (a.y > c.y ? a.y : c.y) : (b.y > c.y ? b.y : c.y);
            const int half = kSubpixelOne / 2;
            // pixel centres within the vertex extents
            int bx0 = (minX - half + kSubpixelOne - 1) >> kSubpixelBits, bx1 = ((maxX - half) >> kSubpixelBits) + 1;
            int by0 = (minY - half + kSubpixelOne - 1) >> kSubpixelBits, by1 = ((maxY - half) >> kSubpixelBits) + 1;
            if (bx0 < 0) bx0 = 0;
            if (by0 < 0) by0 = 0;
            if (bx1 > t.width) bx1 = t.width;
            if (by1 > t.height) by1 = t.height;
            if (bx0 >= bx1 || by0 >= by1) return;
            if ((bx1 - bx0) * (by1 - by0) <= detail::gSmallTrianglePixels) {
                detail::rasterSmall(a, b, c, area, span, *gradient, bx0, by0, bx1, by1);
                return;
            }
        }
        detail::rasterGuarded(t, a, b, c, span, gradient);
        return;
    }
    ScreenVertex poly[16], next[16];
    int n = 3;
    poly[0] = a, poly[1] = b, poly[2] = c;
    for (int plane = 0; plane < 4 && n >= 3; plane++) {
        int m = 0;
        const int axis = plane < 2 ? 0 : 1;
        const int32_t bound = (plane & 1) ? kGuard : -kGuard;
        for (int i = 0; i < n; i++) {
            const ScreenVertex &p = poly[i], &q = poly[(i + 1) % n];
            const bool pin = detail::insideBand(p, plane), qin = detail::insideBand(q, plane);
            if (pin) next[m++] = p;
            if (pin != qin) next[m++] = detail::clipPoint(p, q, axis, bound);
        }
        n = m;
        for (int i = 0; i < n; i++) poly[i] = next[i];
    }
    for (int i = 1; i + 1 < n; i++) detail::rasterGuarded(t, poly[0], poly[i], poly[i + 1], span, gradient);
}

enum RasterFlags { kCullBack = 1, kDepthTest = 2, kDepthWrite = 4 };

void clearTarget(const RasterTarget &t, uint16_t color, uint16_t depth = uint16_t(kDepthMax));
// one flat RGB565 triangle
void drawTriangle(const RasterTarget &t, const ScreenVertex &a, const ScreenVertex &b, const ScreenVertex &c,
                  uint16_t color, int flags, const DepthGradient *gradient = nullptr);

} // namespace sw
} // namespace cr
