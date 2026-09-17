// Software renderer of the SF2000 (task R1.4) behind the class and method names of the GLES2 renderer
// (engine/renderer.h includes this file when CR_FIXED is defined), so the shared scene and UI code draw through either.
// Meshes are flat-colour .fmesh (tools/bake_flat.py): one colour and one axis normal per triangle, lit with the same
// per-axis Lambert term as the GLES shader. The picture is RGB565 with a 16-bit depth buffer; everything is integer or
// 16.16.
#pragma once

#ifndef CR_FIXED
#error "src/sw is the SF2000 renderer: build it with CR_FIXED"
#endif

#include <cstdint>
#include <memory>
#include <vector>

#include "engine/assets.h"
#include "engine/math.h"
#include "sw/raster.h"
#include "sw/sw_math.h"

namespace cr {

namespace sw {
// the painter's order key of a triangle along the view: 0 its centroid, 1 its nearest vertex, 2 its farthest vertex
// (a variable so sw_game --paint-key can compare them)
inline int gPaintKey = 1;
} // namespace sw

struct GpuMesh {
    const FlatMeshData *data = nullptr; // owned by the Renderer
    int indexCount = 0;                 // 3 per triangle, as in the GLES build
    Vec3 aabbMin, aabbMax;
    // the triangles of axis a are data->triangles[axisStart[a] .. axisStart[a + 1]) (tools/bake_flat.py sorts them);
    // axisStart[6] == -1 when a mesh is not sorted
    int axisStart[7] = {0, 0, 0, 0, 0, 0, -1};
    // Renderer::paintOrders_ entry of this mesh (painter's order without a depth buffer), built on first use
    mutable int paintOrder = -1;
};

struct GpuTexture {
    int id = 0;
    int width = 0, height = 0;
};

struct RenderTarget {
    std::vector<uint16_t> color, depth; // RGB565 / depth, rows top first
    int width = 0, height = 0;
};

struct RenderStats {
    int drawCalls = 0;
    int triangles = 0;      // submitted, as the GLES build counts them
    int trianglesDrawn = 0; // after back-face culling
    int spans = 0;          // rows of pixels of 3D triangles and shadows
    int64_t pixels = 0;     // pixels those rows covered (depth-tested)
    // microseconds per stage, measured only while Renderer::profileClock is set (host builds)
    int64_t usTransform = 0, usRaster = 0, usShadows = 0, usOverlay = 0;
};

class Renderer {
public:
    // the default target (bindTarget(nullptr)) is the console's screen
    bool init(int width = 320, int height = 240);

    GpuMesh uploadMesh(const FlatMeshData &mesh);
    // frees the mesh (no-op for an empty one) and resets it
    void releaseMesh(GpuMesh &mesh);

    bool createTarget(RenderTarget &target, int width, int height);
    // nullptr = the default target
    void bindTarget(RenderTarget *target);
    // (x, y) from the bottom-left corner like glViewport; drawing is clipped to the viewport
    void viewport(int x, int y, int w, int h, bool scissor = false);
    void clear(mreal r, mreal g, mreal b);
    // the bound target, converted to RGBA (5/6-bit channels widened by bit replication)
    bool readPixels(int w, int h, std::vector<uint8_t> &rgbaTopFirst);
    const RenderTarget &target() const { return *target_; }

    void setCamera(const Mat4 &projection, const Mat4 &view);
    // direction pointing from the scene towards the light
    void setLightDirection(const Vec3 &towardsLight);

    // the texture is baked into the mesh colours; kept for the GLES signature
    void drawLambert(const GpuMesh &mesh, const GpuTexture &tex, const Mat4 &model);
    // one sRGB colour for the whole mesh; double-sided meshes light their back faces with -N
    void drawFlat(const GpuMesh &mesh, const Vec3 &color, const Mat4 &model, bool doubleSided);

    // Planar shadows: casters are flattened onto a horizontal receiver plane along the light and multiply the picture
    // by `factor`; a mask keeps overlapping shadows from darkening a pixel twice (the GLES stencil).
    void beginShadows(mreal factor);
    void drawShadow(const GpuMesh &mesh, const Mat4 &model, real planeY);
    void endShadows();

    // 2D overlay (HUD, menus) in pixels from the top-left corner of a screenW x screenH screen stretched over the
    // viewport (like the GLES uScreen), over the finished 3D frame. The UI
    // draws axis-aligned quads only (glyphs, images, rectangles); textures are sampled NEAREST like the GLES build and
    // blended in 8 bits per channel before RGB565.
    GpuTexture uploadTexture(const TextureData &tex);
    GpuTexture uploadAlphaTexture(int width, int height, const uint8_t *coverage); // white, alpha = coverage
    void beginOverlay(int screenW, int screenH);
    // quads as 6 vertices of x, y, u, v; imageColors: texture RGBA * colour (images) instead of colour with the
    // texture's alpha (glyphs)
    void drawOverlayTriangles(const GpuTexture &tex, const std::vector<mreal> &xyuv, mreal r, mreal g, mreal b, mreal a,
                              bool imageColors = false);
    void drawOverlayImage(const GpuTexture &tex, mreal x, mreal y, mreal w, mreal h, mreal alpha = 1);
    void drawOverlayRect(mreal x, mreal y, mreal w, mreal h, mreal r, mreal g, mreal b, mreal a);
    void endOverlay();

    // O7.7: an overlay drawn once and then replayed (TextRenderer::drawOutlined on the SF2000). Between
    // beginOverlayCapture and endOverlayCapture the overlay writes into the capture instead of the target. A capture is
    // exact only when every write was opaque (alpha 255 and glyph coverage 0 or 255, as in the baked retro fonts):
    // `valid` says so. drawOverlayCapture copies its covered pixels onto the target.
    struct OverlayCapture {
        int x = 0, y = 0, w = 0, h = 0; // pixels of the viewport
        std::vector<uint16_t> color;
        std::vector<uint8_t> mask;
        bool valid = false;
    };
    void beginOverlayCapture();
    void endOverlayCapture(OverlayCapture &out);
    void drawOverlayCapture(const OverlayCapture &capture);
    // what overlay pixel positions depend on besides the draw arguments: the viewport and beginOverlay's scale
    struct OverlayState {
        int32_t scaleX = 0, scaleY = 0;
        int width = 0, height = 0, stride = 0;
        const uint16_t *origin = nullptr;
        bool operator==(const OverlayState &o) const
        {
            return scaleX == o.scaleX && scaleY == o.scaleY && width == o.width && height == o.height &&
                   stride == o.stride && origin == o.origin;
        }
    };
    OverlayState overlayState() const
    {
        OverlayState s;
        s.scaleX = overlayScaleX_;
        s.scaleY = overlayScaleY_;
        s.width = raster_.width;
        s.height = raster_.height;
        s.stride = raster_.stride;
        s.origin = raster_.color;
        return s;
    }

    GpuMesh unitBox;   // BoxGeometry(1, 1, 1)
    GpuMesh unitPlane; // PlaneGeometry(1, 1): XY plane facing +Z
    FlatMeshData unitBoxData, unitPlaneData;

    RenderStats stats;
    void resetStats()
    {
        stats = RenderStats();
        counters_ = sw::RasterCounters();
    }
    // microsecond clock for the stage times in `stats` (host benchmarks); null = no timing
    uint64_t (*profileClock)() = nullptr;
    // false (the game, O5.3): no depth buffer - the console measured it at a third of the raster (RenderBench).
    // clear() leaves the depth buffer alone, meshes draw their front faces far to near in a triangle order kept per
    // mesh for the four turns about Y (the camera never turns), shadows ignore depth; the caller orders the draws.
    bool depthBuffer = true;
    // without a depth buffer: the parts of the following meshes below this world height (relative to the camera origin,
    // like their matrices) are cut away - what the depth buffer hid under a floor (a log's underwater half, wheels)
    bool clipBelow = false;
    real clipBelowY = 0;
    // without a depth buffer: the following meshes mark their pixels in the shadow mask, so the shadow pass leaves them
    // alone (lily pads drawn with the floors, before the shadows); clearShadowMask() starts the frame's mask before them
    bool shadowProof = false;
    void clearShadowMask();

private:
    void drawMesh(const GpuMesh &mesh, const Mat4 &model, const Vec3 *color, bool doubleSided);
    // vertices are transformed when a drawn triangle first uses them (faces turned away never need theirs)
    void beginVertices(int count);
    const sw::ScreenVertex &vertex(const sw::Mat34 &t, const int32_t *positions, uint16_t index)
    {
        if (stamps_[index] != stamp_) {
            stamps_[index] = stamp_;
            const int32_t *p = positions + index * 3;
            verts_[index] = sw::toScreen(sw::transformQ32(t, p[0], p[1], p[2]));
        }
        return verts_[index];
    }
    std::vector<uint32_t> stamps_;
    uint32_t stamp_ = 0;
    const sw::Mat34 &screen();
    // triangle indices far to near for the mesh turned 0, 90, 180 and 270 degrees about Y, for the view they were
    // sorted for (the screen matrix's depth row); paintOrder() picks the turn closest to the draw's matrix
    struct PaintOrder {
        int32_t view[3] = {0, 0, 0};
        int key = -1; // sw::gPaintKey it was sorted with
        std::vector<uint16_t> turn[4];
    };
    std::vector<PaintOrder> paintOrders_;
    const std::vector<uint16_t> &paintOrder(const GpuMesh &mesh, const sw::Mat34 &t);
    // a triangle crossing clipBelowY: the part above it, cut in model space and drawn as a fan (colour only)
    void drawClippedBelow(const sw::Mat34 &t, const int32_t *positions, const FlatMeshData::Triangle &tri,
                          const int32_t worldY[3], int32_t clip, uint16_t shade);

    std::vector<std::unique_ptr<FlatMeshData>> meshes_;
    RenderTarget default_;
    RenderTarget *target_ = &default_;
    sw::RasterTarget raster_; // the viewport inside target_
    sw::RasterCounters counters_;
    int vpW_ = 0, vpH_ = 0;
    Mat4 projection_ = Mat4::identity(), view_ = Mat4::identity();
    sw::Mat34 screen_{};
    bool screenDirty_ = true;
    Vec3 light_{0, 1, 0};
    std::vector<sw::ScreenVertex> verts_;
    std::vector<uint16_t> shades_;
    struct Texture {
        int width = 0, height = 0;
        bool alphaOnly = false;       // coverage bytes (fonts) instead of RGBA
        std::vector<uint8_t> pixels;  // rows top first
    };
    std::vector<Texture> textures_; // GpuTexture::id - 1
    // pixels whose centres lie in [x0, x1) x [y0, y1); colours and alpha 0..255
    void overlayQuad(const Texture *tex, mreal x0, mreal y0, mreal x1, mreal y1, mreal u0, mreal v0, mreal u1, mreal v1,
                     int r, int g, int b, int a, bool imageColors);
    bool warnedNonQuad_ = false;
    // beginOverlay's screen size mapped onto the viewport, Q16 (the SF2000 draws the 640x480 layouts on 320x240)
    int32_t overlayScaleX_ = 65536, overlayScaleY_ = 65536;
    // O7.7 capture in progress: the viewport-sized buffers the overlay writes into, the box it wrote, whether exact
    bool capturing_ = false, captureExact_ = true;
    std::vector<uint16_t> captureColor_;
    std::vector<uint8_t> captureMask_;
    int captureX0_ = 0, captureY0_ = 0, captureX1_ = 0, captureY1_ = 0;
    std::vector<uint8_t> shadowMask_; // 1 = already darkened in this shadow pass (whole target)
    uint8_t shadowR_[32] = {}, shadowG_[64] = {}, shadowB_[32] = {};
    sw::DepthGradient shadowGradient_{0, 0};
    bool shadowGradientOk_ = false;
};

} // namespace cr
