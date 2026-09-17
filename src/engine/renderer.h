// GLES2 renderer for the voxel models: one Lambert shader reproducing three.js r182
// MeshLambertMaterial with the game's lights (see docs/PROGRESS.md, "wzór światła").
#pragma once

#ifdef CR_FIXED
// SF2000: the software renderer under the same class and method names
#include "sw/renderer_sw.h"
#else

#include <vector>

#include "assets.h"
#include "gl_api.h"
#include "math.h"

namespace cr {

struct GpuMesh {
    GLuint vbo = 0, ibo = 0;
    int indexCount = 0;
    Vec3 aabbMin, aabbMax;
};

struct GpuTexture {
    GLuint id = 0;
    int width = 0, height = 0;
};

struct RenderTarget {
    GLuint fbo = 0, color = 0, depth = 0;
    int width = 0, height = 0;
};

struct RenderStats {
    int drawCalls = 0;
    int triangles = 0;
};

// Upper-left 3x3 of inverse(transpose(model)), for transforming normals under non-uniform scale.
Mat4 normalMatrix(const Mat4 &model);

class Renderer {
public:
    // O19: set from Platform::stencilBits(). The shadow pass masks itself with the stencil so overlapping shadows
    // do not darken a floor twice; on a context without one, that same test throws every shadow fragment away.
    bool hasStencil = true;
    bool init();

    GpuMesh uploadMesh(const MeshData &mesh);
    // frees the buffers (no-op for an empty mesh) and resets it
    void releaseMesh(GpuMesh &mesh);
    GpuTexture uploadTexture(const TextureData &tex);

    bool createTarget(RenderTarget &target, int width, int height);
    // nullptr = default framebuffer
    void bindTarget(const RenderTarget *target);
    void viewport(int x, int y, int w, int h, bool scissor = false);
    void clear(float r, float g, float b);
    bool readPixels(int w, int h, std::vector<uint8_t> &rgbaTopFirst);

    void setCamera(const Mat4 &projection, const Mat4 &view);
    // direction pointing from the scene towards the light (three: light.position - target)
    void setLightDirection(const Vec3 &towardsLight);

    void drawLambert(const GpuMesh &mesh, const GpuTexture &tex, const Mat4 &model);
    // untextured Lambert in an sRGB colour; double-sided planes light their back face with -N
    void drawFlat(const GpuMesh &mesh, const Vec3 &color, const Mat4 &model, bool doubleSided);
    // the same from CPU-side vertices (MeshData layout) through a reused streaming buffer: groups of procedural
    // shapes rebuilt every frame (foam, particles)
    void drawFlatDynamic(const MeshData &mesh, const Vec3 &color, const Mat4 &model, bool doubleSided);

    // Planar shadows: casters are flattened onto a horizontal receiver plane along the light and
    // multiply the framebuffer by `factor`; the stencil keeps overlapping shadows from darkening twice.
    void beginShadows(float factor);
    void drawShadow(const GpuMesh &mesh, const Mat4 &model, float planeY);
    void endShadows();

    // 2D overlay (HUD, menus) in pixels from the top-left corner, drawn over the finished 3D frame
    GpuTexture uploadAlphaTexture(int width, int height, const uint8_t *coverage); // white, alpha = coverage
    void beginOverlay(int screenW, int screenH);
    // triangles as x, y, u, v per vertex; imageColors: texture RGBA * colour (images) instead of colour with the
    // texture's alpha (glyphs)
    void drawOverlayTriangles(const GpuTexture &tex, const std::vector<float> &xyuv, float r, float g, float b, float a,
                              bool imageColors = false);
    // an RGBA image (uploadTexture) stretched to the rectangle, faded by alpha
    void drawOverlayImage(const GpuTexture &tex, float x, float y, float w, float h, float alpha = 1);
    // solid rectangle: banners, tints, screen fades
    void drawOverlayRect(float x, float y, float w, float h, float r, float g, float b, float a);
    void endOverlay();

    GpuMesh unitBox;   // BoxGeometry(1, 1, 1)
    GpuMesh unitPlane; // PlaneGeometry(1, 1): XY plane facing +Z
    MeshData unitBoxData, unitPlaneData; // CPU copies of the two

    RenderStats stats;
    void resetStats() { stats = RenderStats(); }

private:
    void bindVertexLayout(const GpuMesh &mesh);

    GLuint lambert_ = 0;
    GLint uViewProj_ = -1, uModel_ = -1, uNormal_ = -1, uLight_ = -1, uTex_ = -1;
    GLuint flat_ = 0;
    GLint fViewProj_ = -1, fModel_ = -1, fNormal_ = -1, fLight_ = -1, fColor_ = -1;
    GLuint shadow_ = 0;
    GLint sMvp_ = -1, sColor_ = -1;
    GLuint overlay_ = 0;
    GLint oScreen_ = -1, oColor_ = -1, oTex_ = -1, oMode_ = -1;
    GpuTexture whiteTexture_;
    GpuMesh stream_;
    size_t streamVertexBytes_ = 0, streamIndexBytes_ = 0;
    std::vector<float> overlayScratch_;
    float shadowFactor_ = 1;
    Mat4 viewProj_ = Mat4::identity();
    Vec3 light_{0, 1, 0};
};

} // namespace cr

#endif // CR_FIXED
