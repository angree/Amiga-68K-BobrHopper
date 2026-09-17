// The SF2000 SceneRenderer (task R1.5): game/scene_render.h includes this file when CR_FIXED is defined. Same public
// names as the GLES version (init, render, setupCamera, measureFraming, shadowMode, viewShift, framing, view,
// projection, culled, shadowCasters, drawsByKind) with 16.16 numbers. The scene graph is drawn relative to the
// camera position (small translations keep the 16.16 screen maths exact); row batching and shape groups of the GLES
// build only saved draw calls, so every node is drawn on its own.
#pragma once

#ifndef CR_FIXED
#error "src/sw is the SF2000 renderer: build it with CR_FIXED"
#endif

#include <map>
#include <string>
#include <vector>

#include "engine/renderer.h"
#include "game/game.h"

namespace cr {

struct Manifest;

// Full: every caster like the original; Simple: every caster as its bounding box (cheap; O3.5: the hero/vehicles/logs
// meshes it drew before were thin strips beside their casters, invisible on the SF2000, and still cost time); Off: none
enum class ShadowMode { Full, Simple, Off };

// what a given view scale / shift shows around the hero (GLES scene_render.h)
struct FramingInfo {
    real heroScreenY = 0; // hero's feet: 0 = top edge, 1 = bottom edge
    int heroHeightPx = 0;
    int rowsAhead = 0;
    int rowsBehind = 0;
    bool laneAtHero = false;
    bool laneAtTop = false;
};

class SceneRenderer {
public:
    // loads <dataDir>/meshes/<model>.fmesh for every model and points the ModelLibrary's models at them
    bool init(Renderer &renderer, ModelLibrary &models, const Manifest &manifest, const std::string &dataDir);

    void render(Renderer &renderer, Game &game, int width, int height, mreal viewScale);
    void setupCamera(Game &game, int width, int height, mreal viewScale);
    void measureFraming(Game &game, int height);

    ShadowMode shadowMode = ShadowMode::Full;
    bool batchStatic = true; // no effect here (GLES draw-call batching)
    mreal viewShift = 0;
    FramingInfo framing;
    Mat4 view = Mat4::identity(), projection = Mat4::identity(); // world space, as in the GLES build
    int culled = 0;
    int culledRows = 0; // O7.2: map rows left out whole (their world matrices included)
    int shadowCasters = 0;
    enum DrawKind { KindFloor, KindObstacle, KindEntity, KindHero, KindShape, KindShadow, KindCount };
    int drawsByKind[KindCount] = {};
    // diagnostics (sw_game --model-stats): every drawn model with the triangles it drew after culling
    std::vector<std::pair<const Model *, int>> *drawLog = nullptr;
    // false (the game, O5.3): no depth buffer - floors far to near, then the shadows onto them, then everything else far
    // to near (Renderer::depthBuffer); nodes are drawn with a 1.5 view margin, only shadow casters keep 2.5.
    // true: the depth-buffered renderer of v003-v008, drawn in scene order (sw_game --depth, comparisons)
    bool depthBuffer = false;
    // O16: the log the hero rides is drawn just before the hero, whatever their centres say (renderScene).
    // sw_game --no-ride-fix turns it off, so a test can render one frame both ways and see which sits closer to the
    // depth-buffered reference (build/log_ride_check.sh). rideFrames counts the frames the hero spent on a log,
    // ridePushed those where the log would otherwise have been drawn over the hero.
    bool rideFix = true;
    int rideFrames = 0, ridePushed = 0;
    // O5.5 profile of the last render() in microseconds, while profileClock is set (the SF2000 core's game report)
    struct Profile {
        int64_t updateWorld = 0, traverse = 0, sort = 0, floors = 0, shadows = 0, objects = 0, framing = 0;
    };
    Profile profile;
    uint64_t (*profileClock)() = nullptr;

private:
    struct Caster {
        const GpuMesh *mesh;
        Mat4 world;  // relative to the camera origin
        real planeY; // relative to the camera origin
    };
    struct Item {
        Node *node;
        Mat4 world;  // relative to the camera origin
        int64_t far; // distance from the camera along its view, Q16 (larger = farther)
        bool clip;   // without a depth buffer: cut away below clipY (the surface the node stands on)
        real clipY;  // relative to the camera origin
    };
    bool planeKnown_ = false; // the planeY handed down belongs to a floor (or the hero's surface)
    void drawNode(Renderer &renderer, Node *node, real planeY);
    void drawItem(Renderer &renderer, const Item &item);
    void drawShadows(Renderer &renderer);
    std::vector<Item> floors_, objects_;
    // (distance, index) pairs sorted instead of the ~90-byte items: stable_sort moved whole matrices and allocated a
    // buffer every frame, which cost ~1.5 ms per frame on MIPS under qemu (memory is what the console lacks)
    std::vector<std::pair<int64_t, uint16_t>> order_;
    void drawFarToNear(Renderer &renderer, const std::vector<Item> &items, int64_t &drawTime);
    // the view distance of the floor being visited (painter's order): its lily pads draw right after it
    int64_t floorFar_ = 0;
    void renderScene(Renderer &renderer, Game &game, int height);

    std::map<std::string, GpuMesh> meshes_;
    GpuTexture texture_; // the models' colours are in their meshes
    std::vector<Caster> casters_;
    Vec3 origin_;                                   // camera position: what node translations are drawn relative to
    Mat4 viewRelative_ = Mat4::identity();          // camera rotation only
    Mat4 viewProjRelative_ = Mat4::identity();
    Node *hero_ = nullptr;
    // O7.2: most of the map pool's 80 rows (all kinds, parked or long behind) are off screen; a row the camera cannot see
    // gets no world matrices and no traversal
    const Node *rowParent_ = nullptr, *particlesA_ = nullptr, *particlesB_ = nullptr;
    bool rowOutside(const Node *row) const;
    void updateVisibleWorld(Node *node, const Mat4 &parentWorld);
    bool inHero_ = false;
    real heroPlane_ = 0;
    // O16: whole objects sort by the distance of their centre, so a hero sitting far enough towards +x along a long
    // log sorts nearer than the log's centre and the log is drawn over it (build/log_ride_check.sh). The ridden log's
    // items are pushed just behind the hero's before they are drawn; a subtree's items are contiguous (depth first),
    // so a begin/end pair finds them again after the traversal.
    const Node *ridingNode_ = nullptr;
    size_t heroBegin_ = 0, heroEnd_ = 0, ridingBegin_ = 0, ridingEnd_ = 0;
};

} // namespace cr
