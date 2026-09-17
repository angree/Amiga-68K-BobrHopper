#include "renderer.h"

#include <algorithm>
#include <string>

#include "log.h"

namespace cr {

namespace {

// Light model, derived from three r182 (lights_lambert_pars_fragment + BRDF_Lambert):
//   linear = sRGB_EOTF(tex) * (ambient 1.8 + directional 1.0 * max(N.L, 0)) / PI
//   out    = sRGB_OETF(linear)
// approximated per vertex as tex * ((1.8 + N.L) / PI)^(1/2.2): worst error 2.64/255 over every
// colour in the game's textures (measured, docs/PROGRESS.md), and no pow() per fragment.
const char *kLambertVS = R"(
attribute vec3 aPos;
attribute vec3 aNormal;
attribute vec2 aUV;
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat4 uNormal;
uniform vec3 uLight;
varying vec2 vUV;
varying float vLight;
void main() {
  vec3 n = normalize((uNormal * vec4(aNormal, 0.0)).xyz);
  float d = max(dot(n, uLight), 0.0);
  vLight = pow((1.8 + d) * 0.31830989, 0.45454545);
  vUV = aUV;
  gl_Position = uViewProj * (uModel * vec4(aPos, 1.0));
}
)";

const char *kLambertFS = R"(
uniform sampler2D uTex;
varying vec2 vUV;
varying float vLight;
void main() {
  gl_FragColor = vec4(texture2D(uTex, vUV).rgb * vLight, 1.0);
}
)";

const char *kFlatVS = R"(
attribute vec3 aPos;
attribute vec3 aNormal;
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat4 uNormal;
uniform vec3 uLight;
varying float vFront;
varying float vBack;
void main() {
  vec3 n = normalize((uNormal * vec4(aNormal, 0.0)).xyz);
  vFront = pow((1.8 + max(dot(n, uLight), 0.0)) * 0.31830989, 0.45454545);
  vBack = pow((1.8 + max(dot(-n, uLight), 0.0)) * 0.31830989, 0.45454545);
  gl_Position = uViewProj * (uModel * vec4(aPos, 1.0));
}
)";

const char *kFlatFS = R"(
uniform vec3 uColor;
varying float vFront;
varying float vBack;
void main() {
  gl_FragColor = vec4(uColor * (gl_FrontFacing ? vFront : vBack), 1.0);
}
)";

// 2D overlay (HUD, menus): positions in pixels from the top-left corner, alpha from the texture
const char *kOverlayVS = R"(
attribute vec2 aPos;
attribute vec2 aUV;
uniform vec4 uScreen;
varying vec2 vUV;
void main() {
  vUV = aUV;
  gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
}
)";

// uMode.x = 0: glyph/rect (colour from uColor, alpha from the texture); 1: image (texture colours * uColor)
const char *kOverlayFS = R"(
uniform sampler2D uTex;
uniform vec4 uColor;
uniform vec4 uMode;
varying vec2 vUV;
void main() {
  vec4 t = texture2D(uTex, vUV);
  gl_FragColor = uMode.x > 0.5 ? t * uColor : vec4(uColor.rgb, uColor.a * t.a);
}
)";

const char *kShadowVS = R"(
attribute vec3 aPos;
uniform mat4 uMvp;
void main() { gl_Position = uMvp * vec4(aPos, 1.0); }
)";

const char *kShadowFS = R"(
uniform vec3 uColor;
void main() { gl_FragColor = vec4(uColor, 1.0); }
)";

MeshData makeBox()
{
    MeshData m;
    // per face: normal n, tangent u, bitangent v with u x v = n, so corners run counter-clockwise outside
    static const float faces[6][9] = {
        {1, 0, 0, 0, 0, -1, 0, 1, 0},  {-1, 0, 0, 0, 0, 1, 0, 1, 0}, {0, 1, 0, 1, 0, 0, 0, 0, -1},
        {0, -1, 0, 1, 0, 0, 0, 0, 1},  {0, 0, 1, 1, 0, 0, 0, 1, 0},  {0, 0, -1, -1, 0, 0, 0, 1, 0},
    };
    for (int f = 0; f < 6; f++) {
        float nx = faces[f][0], ny = faces[f][1], nz = faces[f][2];
        float ux = faces[f][3], uy = faces[f][4], uz = faces[f][5];
        float vx = faces[f][6], vy = faces[f][7], vz = faces[f][8];
        uint16_t base = uint16_t(m.vertices.size() / 8);
        static const float corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (int c = 0; c < 4; c++) {
            float a = corner[c][0] * 0.5f, b = corner[c][1] * 0.5f;
            float v[8] = {nx * 0.5f + ux * a + vx * b, ny * 0.5f + uy * a + vy * b, nz * 0.5f + uz * a + vz * b,
                          nx, ny, nz, 0, 0};
            m.vertices.insert(m.vertices.end(), v, v + 8);
        }
        uint16_t idx[6] = {base, uint16_t(base + 1), uint16_t(base + 2), base, uint16_t(base + 2), uint16_t(base + 3)};
        m.indices.insert(m.indices.end(), idx, idx + 6);
    }
    for (int i = 0; i < 3; i++) {
        m.aabbMin[i] = -0.5f;
        m.aabbMax[i] = 0.5f;
    }
    return m;
}

MeshData makePlane()
{
    MeshData m;
    const float v[4][8] = {{-0.5f, -0.5f, 0, 0, 0, 1, 0, 0},
                           {0.5f, -0.5f, 0, 0, 0, 1, 0, 0},
                           {0.5f, 0.5f, 0, 0, 0, 1, 0, 0},
                           {-0.5f, 0.5f, 0, 0, 0, 1, 0, 0}};
    for (auto &row : v) m.vertices.insert(m.vertices.end(), row, row + 8);
    m.indices = {0, 1, 2, 0, 2, 3};
    m.aabbMin[0] = m.aabbMin[1] = -0.5f;
    m.aabbMax[0] = m.aabbMax[1] = 0.5f;
    return m;
}

GLuint compileShader(GLenum type, const char *body)
{
    std::string src = std::string(gl::shaderPreamble()) + body;
    const char *p = src.c_str();
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char info[2048] = {0};
        glGetShaderInfoLog(s, sizeof info, nullptr, info);
        logf("renderer: shader compile failed: %s", info);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint linkProgram(const char *vs, const char *fs, const char *const *attribs, int attribCount)
{
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    for (int i = 0; i < attribCount; i++) glBindAttribLocation(p, GLuint(i), attribs[i]);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char info[2048] = {0};
        glGetProgramInfoLog(p, sizeof info, nullptr, info);
        logf("renderer: program link failed: %s", info);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

} // namespace

Mat4 normalMatrix(const Mat4 &model)
{
    Mat4 inv = inverse(model);
    Mat4 n = Mat4::identity();
    for (int c = 0; c < 3; c++)
        for (int r = 0; r < 3; r++) n.e[c * 4 + r] = inv.e[r * 4 + c];
    return n;
}

bool Renderer::init()
{
    static const char *const attribs[] = {"aPos", "aNormal", "aUV"};
    lambert_ = linkProgram(kLambertVS, kLambertFS, attribs, 3);
    if (!lambert_) return false;
    uViewProj_ = glGetUniformLocation(lambert_, "uViewProj");
    uModel_ = glGetUniformLocation(lambert_, "uModel");
    uNormal_ = glGetUniformLocation(lambert_, "uNormal");
    uLight_ = glGetUniformLocation(lambert_, "uLight");
    uTex_ = glGetUniformLocation(lambert_, "uTex");
    flat_ = linkProgram(kFlatVS, kFlatFS, attribs, 2);
    if (!flat_) return false;
    fViewProj_ = glGetUniformLocation(flat_, "uViewProj");
    fModel_ = glGetUniformLocation(flat_, "uModel");
    fNormal_ = glGetUniformLocation(flat_, "uNormal");
    fLight_ = glGetUniformLocation(flat_, "uLight");
    fColor_ = glGetUniformLocation(flat_, "uColor");
    shadow_ = linkProgram(kShadowVS, kShadowFS, attribs, 1);
    if (!shadow_) return false;
    sMvp_ = glGetUniformLocation(shadow_, "uMvp");
    sColor_ = glGetUniformLocation(shadow_, "uColor");
    static const char *const overlayAttribs[] = {"aPos", "aUV"};
    overlay_ = linkProgram(kOverlayVS, kOverlayFS, overlayAttribs, 2);
    if (!overlay_) return false;
    oScreen_ = glGetUniformLocation(overlay_, "uScreen");
    oColor_ = glGetUniformLocation(overlay_, "uColor");
    oTex_ = glGetUniformLocation(overlay_, "uTex");
    oMode_ = glGetUniformLocation(overlay_, "uMode");
    const uint8_t opaque = 255;
    whiteTexture_ = uploadAlphaTexture(1, 1, &opaque);
    unitBoxData = makeBox();
    unitPlaneData = makePlane();
    unitBox = uploadMesh(unitBoxData);
    unitPlane = uploadMesh(unitPlaneData);
    glGenBuffers(1, &stream_.vbo);
    glGenBuffers(1, &stream_.ibo);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    return true;
}

GpuMesh Renderer::uploadMesh(const MeshData &mesh)
{
    GpuMesh g;
    glGenBuffers(1, &g.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(mesh.vertices.size() * sizeof(float)), mesh.vertices.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &g.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(mesh.indices.size() * sizeof(uint16_t)), mesh.indices.data(),
                 GL_STATIC_DRAW);
    g.indexCount = int(mesh.indices.size());
    g.aabbMin = {mesh.aabbMin[0], mesh.aabbMin[1], mesh.aabbMin[2]};
    g.aabbMax = {mesh.aabbMax[0], mesh.aabbMax[1], mesh.aabbMax[2]};
    return g;
}

void Renderer::releaseMesh(GpuMesh &mesh)
{
    if (mesh.vbo) glDeleteBuffers(1, &mesh.vbo);
    if (mesh.ibo) glDeleteBuffers(1, &mesh.ibo);
    mesh = GpuMesh();
}

GpuTexture Renderer::uploadTexture(const TextureData &tex)
{
    GpuTexture g;
    glGenTextures(1, &g.id);
    glBindTexture(GL_TEXTURE_2D, g.id);
    GLenum fmt = tex.channels == 4 ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, GLint(fmt), tex.width, tex.height, 0, fmt, GL_UNSIGNED_BYTE, tex.pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g.width = tex.width;
    g.height = tex.height;
    return g;
}

GpuTexture Renderer::uploadAlphaTexture(int width, int height, const uint8_t *coverage)
{
    std::vector<uint8_t> rgba(size_t(width) * size_t(height) * 4, 255);
    for (size_t i = 0, n = size_t(width) * size_t(height); i < n; i++) rgba[i * 4 + 3] = coverage[i];
    GpuTexture g;
    glGenTextures(1, &g.id);
    glBindTexture(GL_TEXTURE_2D, g.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g.width = width;
    g.height = height;
    return g;
}

void Renderer::beginOverlay(int screenW, int screenH)
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(overlay_);
    glUniform4f(oScreen_, float(screenW), float(screenH), 0, 0);
    glUniform1i(oTex_, 0);
    // client-side vertex arrays: nothing may stay bound to the buffer targets
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
}

void Renderer::drawOverlayTriangles(const GpuTexture &tex, const std::vector<float> &xyuv, float r, float g, float b,
                                    float a, bool imageColors)
{
    if (xyuv.size() < 12) return;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glUniform4f(oColor_, r, g, b, a);
    glUniform4f(oMode_, imageColors ? 1.0f : 0.0f, 0, 0, 0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), xyuv.data());
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), xyuv.data() + 2);
    GLsizei count = GLsizei(xyuv.size() / 4);
    glDrawArrays(GL_TRIANGLES, 0, count);
    stats.drawCalls++;
    stats.triangles += count / 3;
}

static void quad(std::vector<float> &out, float x, float y, float w, float h, float u0, float v0, float u1, float v1)
{
    const float x1 = x + w, y1 = y + h;
    const float q[24] = {x, y, u0, v0, x1, y, u1, v0, x1, y1, u1, v1, x, y, u0, v0, x1, y1, u1, v1, x, y1, u0, v1};
    out.assign(q, q + 24);
}

void Renderer::drawOverlayImage(const GpuTexture &tex, float x, float y, float w, float h, float alpha)
{
    quad(overlayScratch_, x, y, w, h, 0, 0, 1, 1);
    drawOverlayTriangles(tex, overlayScratch_, 1, 1, 1, alpha, true);
}

void Renderer::drawOverlayRect(float x, float y, float w, float h, float r, float g, float b, float a)
{
    quad(overlayScratch_, x, y, w, h, 0, 0, 1, 1);
    drawOverlayTriangles(whiteTexture_, overlayScratch_, r, g, b, a, false);
}

void Renderer::endOverlay()
{
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

bool Renderer::createTarget(RenderTarget &t, int width, int height)
{
    t.width = width;
    t.height = height;
    glGenTextures(1, &t.color);
    glBindTexture(GL_TEXTURE_2D, t.color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color, 0);
    glGenRenderbuffers(1, &t.depth);
    glBindRenderbuffer(GL_RENDERBUFFER, t.depth);
    // packed depth+stencil when available (planar shadows use the stencil), else depth only
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depth);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, t.depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depth);
    }
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!ok) logf("renderer: framebuffer %dx%d incomplete", width, height);
    return ok;
}

void Renderer::bindTarget(const RenderTarget *t)
{
    glBindFramebuffer(GL_FRAMEBUFFER, t ? t->fbo : 0);
}

void Renderer::viewport(int x, int y, int w, int h, bool scissor)
{
    glViewport(x, y, w, h);
    if (scissor) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(x, y, w, h);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
}

void Renderer::clear(float r, float g, float b)
{
    glClearColor(r, g, b, 1);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

bool Renderer::readPixels(int w, int h, std::vector<uint8_t> &out)
{
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    out.resize(px.size());
    for (int y = 0; y < h; y++)
        std::copy(px.begin() + long(size_t(h - 1 - y) * size_t(w) * 4), px.begin() + long(size_t(h - y) * size_t(w) * 4),
                  out.begin() + long(size_t(y) * size_t(w) * 4));
    return glGetError() == GL_NO_ERROR;
}

void Renderer::setCamera(const Mat4 &projection, const Mat4 &view)
{
    viewProj_ = projection * view;
}

void Renderer::setLightDirection(const Vec3 &towardsLight)
{
    light_ = normalize(towardsLight);
}

void Renderer::drawLambert(const GpuMesh &mesh, const GpuTexture &tex, const Mat4 &model)
{
    glUseProgram(lambert_);
    glUniformMatrix4fv(uViewProj_, 1, GL_FALSE, viewProj_.e);
    glUniformMatrix4fv(uModel_, 1, GL_FALSE, model.e);
    Mat4 nm = normalMatrix(model);
    glUniformMatrix4fv(uNormal_, 1, GL_FALSE, nm.e);
    glUniform3f(uLight_, light_.x, light_.y, light_.z);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glUniform1i(uTex_, 0);

    bindVertexLayout(mesh);
    glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_SHORT, nullptr);
    stats.drawCalls++;
    stats.triangles += mesh.indexCount / 3;
}

void Renderer::bindVertexLayout(const GpuMesh &mesh)
{
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(3 * sizeof(float)));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(6 * sizeof(float)));
}

void Renderer::beginShadows(float factor)
{
    shadowFactor_ = factor;
    glUseProgram(shadow_);
    glUniform3f(sColor_, factor, factor, factor);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_SRC_COLOR); // dst *= factor
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);            // flattened geometry has arbitrary winding
    // O19: the flat 0.003 lift above the floor is a fixed distance, so it buys only a couple of depth steps on a
    // 16-bit buffer and none at all where a triangle is steeply foreshortened. A polygon offset scales with the
    // depth slope of each triangle, which is what stops parts of a shadow dropping out as the camera moves
    // (user, R36S: "cienie mrugaja przy przesuwaniu sie kamery ... zanikaja ich czesci jakby poligony").
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -2.0f); // towards the viewer: the shadow must win GL_LESS against its own floor
    // O19: the stencil keeps overlapping shadows from darkening a floor twice - but only if the context has one.
    // A driver may hand back a config with no stencil (the R36S is a real candidate), and then GL_EQUAL against a
    // buffer that is not there discards the fragments instead: whole shadows vanish, which is what the user saw
    // ("zanikaja ich czesci jakby poligony"). Without a stencil, draw the shadows plainly and accept that two
    // overlapping ones darken a little more.
    if (hasStencil) {
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_EQUAL, 0, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    }
}

void Renderer::drawShadow(const GpuMesh &mesh, const Mat4 &model, float planeY)
{
    // P' = P - L * (P.y - h) / L.y, slightly above the receiver to win the depth test
    const float h = planeY + 0.003f;
    const float kx = light_.x / light_.y, kz = light_.z / light_.y;
    Mat4 flatten = Mat4::identity();
    flatten.e[4] = -kx;   // x' = x - kx * (y - h)
    flatten.e[12] = kx * h;
    flatten.e[5] = 0;     // y' = h
    flatten.e[13] = h;
    flatten.e[6] = -kz;   // z' = z - kz * (y - h)
    flatten.e[14] = kz * h;
    Mat4 mvp = viewProj_ * (flatten * model);
    glUniformMatrix4fv(sMvp_, 1, GL_FALSE, mvp.e);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);
    glEnableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
    glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_SHORT, nullptr);
    stats.drawCalls++;
    stats.triangles += mesh.indexCount / 3;
}

void Renderer::endShadows()
{
    if (hasStencil) glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(0.0f, 0.0f);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
}

void Renderer::drawFlat(const GpuMesh &mesh, const Vec3 &color, const Mat4 &model, bool doubleSided)
{
    glUseProgram(flat_);
    glUniformMatrix4fv(fViewProj_, 1, GL_FALSE, viewProj_.e);
    glUniformMatrix4fv(fModel_, 1, GL_FALSE, model.e);
    Mat4 nm = normalMatrix(model);
    glUniformMatrix4fv(fNormal_, 1, GL_FALSE, nm.e);
    glUniform3f(fLight_, light_.x, light_.y, light_.z);
    glUniform3f(fColor_, color.x, color.y, color.z);
    if (doubleSided) glDisable(GL_CULL_FACE);
    bindVertexLayout(mesh);
    glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_SHORT, nullptr);
    if (doubleSided) glEnable(GL_CULL_FACE);
    stats.drawCalls++;
    stats.triangles += mesh.indexCount / 3;
}

void Renderer::drawFlatDynamic(const MeshData &mesh, const Vec3 &color, const Mat4 &model, bool doubleSided)
{
    if (mesh.indices.empty()) return;
    const size_t vertexBytes = mesh.vertices.size() * sizeof(float);
    const size_t indexBytes = mesh.indices.size() * sizeof(uint16_t);
    glBindBuffer(GL_ARRAY_BUFFER, stream_.vbo);
    if (vertexBytes > streamVertexBytes_) {
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertexBytes), mesh.vertices.data(), GL_DYNAMIC_DRAW);
        streamVertexBytes_ = vertexBytes;
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(vertexBytes), mesh.vertices.data());
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, stream_.ibo);
    if (indexBytes > streamIndexBytes_) {
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(indexBytes), mesh.indices.data(), GL_DYNAMIC_DRAW);
        streamIndexBytes_ = indexBytes;
    } else {
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, GLsizeiptr(indexBytes), mesh.indices.data());
    }
    stream_.indexCount = int(mesh.indices.size());
    drawFlat(stream_, color, model, doubleSided);
}

} // namespace cr
