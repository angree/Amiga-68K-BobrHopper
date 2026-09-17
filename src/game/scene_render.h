// Draws a Game's scene graph with the CrossyCamera and the game's lights.
#pragma once

#ifdef CR_FIXED
// SF2000: the software scene renderer under the same names
#include "sw/scene_render_sw.h"
#else

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/renderer.h"
#include "game/game.h"

namespace cr {

struct Manifest;

// Full: every caster like the original; Simple: every caster as its bounding box (a few triangles each; O3.5: the
// hero/vehicles/logs meshes it drew before were thin strips beside their casters, invisible on the SF2000); Off: none.
// On the R36S all three run at the 78 Hz display rate (docs/PROGRESS.md).
enum class ShadowMode { Full, Simple, Off };

// K.1: what a given view scale / shift shows around the hero, measured on the rendered frame
struct FramingInfo {
    double heroScreenY = 0;     // hero's feet: 0 = top edge, 1 = bottom edge
    int heroHeightPx = 0;       // on-screen height of the character model
    int rowsAhead = 0;          // consecutive row centres (x = 0) on screen in front of the hero's row
    int rowsBehind = 0;         // ... and behind it
    bool laneAtHero = false;    // x = -4.5 and 4.5 (playable -4..4 plus a wall) both on screen at the hero's row
    bool laneAtTop = false;     // ... at the furthest visible row ahead
};

class SceneRenderer {
public:
    // uploads every mesh/texture once and points the ModelLibrary's models at them
    bool init(Renderer &renderer, ModelLibrary &models, const Manifest &manifest, const std::string &dataDir);

    // viewScale plays the role of Dimensions.scale in CrossyCamera.updateScale
    void render(Renderer &renderer, Game &game, int width, int height, float viewScale);

    // camera matrices only (no GL): view/projection for this frame size, scale and viewShift
    void setupCamera(Game &game, int width, int height, float viewScale);
    // fills `framing` from the current camera; world matrices must be up to date (updateWorld)
    void measureFraming(Game &game, int height);

    ShadowMode shadowMode = ShadowMode::Full;
    // 6.5b: a grass row's obstacles drawn as one pre-transformed mesh per texture plus one shadow pass
    // (--no-batch draws them one by one, for A/B screenshots)
    bool batchStatic = true;
    // port: vertical shift of the picture in NDC units (positive = scene moves down, more rows ahead of the
    // hero); render-only, the game logic never sees it
    float viewShift = 0;
    FramingInfo framing; // measured by the last render()
    Mat4 view = Mat4::identity(), projection = Mat4::identity();
    int culled = 0;
    int shadowCasters = 0;
    // draw calls of the last frame by kind (6.5 budget): floors, static obstacles, moving entities (cars, trucks,
    // trains, logs, lily pads, train lights), the hero, procedural shapes (particles, foam), shadow passes
    enum DrawKind { KindFloor, KindObstacle, KindEntity, KindHero, KindShape, KindShadow, KindCount };
    int drawsByKind[KindCount] = {};

private:
    struct Caster {
        const GpuMesh *mesh;
        Mat4 world;
        float planeY;
    };
    void drawNode(Renderer &renderer, Node *node, const Mat4 &viewProj, float planeY);
    void renderScene(Renderer &renderer, Game &game, int height);

    struct RowBatch {
        uint64_t signature = 0;
        struct Group {
            const GpuTexture *texture = nullptr;
            GpuMesh mesh;
        };
        std::vector<Group> groups;
        GpuMesh shadow; // every shadow-casting obstacle of the row
        bool castsShadow = false;
        int count = 0;
        Vec3 min, max; // floor-local bounds
    };
    // true when `floor` is a grass row floor whose children were drawn (or culled) as its batch
    bool drawRowBatch(Renderer &renderer, Node *floor, const Mat4 &viewProj, float planeY);
    void buildRowBatch(Renderer &renderer, const Node *floor, RowBatch &batch);
    // 6.5c: true when every child of `group` is the same procedural shape (a foam strip, a particle system);
    // they are drawn as one mesh rebuilt this frame
    bool drawShapeGroup(Renderer &renderer, Node *group, const Mat4 &viewProj);
    MeshData shapeScratch_;
    std::map<const GpuMesh *, MeshData> meshData_; // CPU copies for batching
    std::unordered_map<const Node *, RowBatch> rowBatches_;

    // the obstacle textures (trees, boulders) packed into one, so a row batch is a single draw call
    struct AtlasRect {
        float x, y, w, h; // pixels
    };
    void buildAtlas(Renderer &renderer, const std::map<const GpuTexture *, TextureData> &textures);
    GpuTexture atlas_;
    int atlasW_ = 0, atlasH_ = 0;
    std::map<const GpuTexture *, AtlasRect> atlasRects_;
    std::map<std::string, GpuMesh> meshes_;
    std::map<std::string, GpuTexture> textures_;
    std::vector<Caster> casters_;
    Node *hero_ = nullptr;
    bool inHero_ = false; // drawNode is inside the hero's subtree
    float heroPlane_ = 0;
};

} // namespace cr

#endif // CR_FIXED
