// Amiga sprite baker (task C1): every model rendered ONCE, offline, through the game's own camera, cropped to its
// pixels, and written with the anchor the Amiga needs to place it.
//
//   sw_bake_amiga.exe --data data_sf2000 --out out/check/amiga/sprites [--view-scale 6] [--only tree_0]
//   sw_bake_amiga.exe --calibrate            measure the world -> screen projection
//   sw_bake_amiga.exe --info                 every model's world size and projected size (finds canvas clipping)
//
// WHY SPRITES ARE FAITHFUL HERE AND NOT AN APPROXIMATION: the game's camera is ORTHOGRAPHIC and never rotates
// (src/game/settings.h: OrthographicCamera, lookAt(0,0,0) from (-1, 2.8, -2.9)). Under an orthographic projection an
// object's screen size does not depend on how far away it is, so one baked picture of a model is correct at every
// position it can ever occupy. The camera does not even translate: Game::forwardScene slides the WORLD in x and z,
// which under a linear projection is a pure 2D translation of the whole picture.
//
// It draws with the SF2000 software renderer (CR_FIXED, flat colours, same Lambert term), so the sprites look like
// the version we already ship rather than like a second interpretation of the art.
//
// Three decisions worth knowing:
//   * Background is detected by rendering every sprite TWICE against two different clear colours, calling a pixel
//     background only where the two disagree. The obvious alternative - clearing to "a colour the art never uses" -
//     is a guess that silently eats matching pixels of the model.
//   * The hero is baked as 4 rotations x N squash phases. The hop's vertical arc is PLACEMENT (the Amiga blits the
//     sprite higher), while the squash/stretch in Player::commitMovementAnimations is what changes the pixels.
//   * ROW FLOORS ARE ONE SPRITE EACH, not a repeating tile. A row is 25x h x 1 world units, which is 798 px wide at
//     the game's scale - it fits on a wide canvas whole. Tiling was tried first and measured: the row is NOT
//     periodic at 1..6 units (grass carries three large colour zones along its length; the mismatch map showed the
//     error sitting exactly on those two boundaries), so a tile would have thrown that variation away. One clipped
//     blit per row is also cheaper on a 68020 than ten cell blits. Rows never rotate, so they get rotation 0 only.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine/assets.h"
#include "engine/log.h"
#include "engine/math.h"
#include "engine/png_write.h"
#include "engine/renderer.h"
#include "game/settings.h"

using namespace cr;

namespace {

// Wide enough for the widest thing in the game - a 25-unit row floor projects to 798 x 214 px - and tall enough for
// the tallest (a tree at 67 px, a train at 103). Everything is cropped to its own pixels afterwards, so the only
// cost of a generous canvas is bake time.
int gCanvasW = 1024, gCanvasH = 384;
// THE RASTERISER DRAWS NOTHING AT x >= 1024 - measured: a beaver baked on a 2032-wide canvas lost every pixel right of
// column 1024, and a row floor came out 807 px instead of ~1600. The 640x480 set needs floors that wide, so a canvas
// wider than this is rendered in TILES of this width, each with the projection shifted by a whole number of tiles
// (a power-of-two fraction of NDC, so the seams are exact), and composited. gCanvasW stays the virtual width.
const int kMaxTileW = 1024;
int gTileW = 1024;

// The squash/stretch states the hero passes through; the Amiga picks the nearest one from the live scale.y.
const double kHeroPhases[] = {0.80, 0.85, 0.90, 0.95, 1.00, 1.05, 1.10, 1.15, 1.20};
constexpr int kHeroSquashCount = int(sizeof(kHeroPhases) / sizeof(kHeroPhases[0]));
// AFTER A COLLISION the game does not squash the hero a little - it flattens him, and the first Amiga drafts showed
// the healthy sprite lying there. Two more "phases", with the exact scales of src/game/player.cpp and rows.cpp:
//   kHeroSquashCount     run over by a car or a train: scale (1.7, 0.05, 1.7) - a pancake on the road
//   kHeroSquashCount + 1 hit from the side while hopping: scale (1, 1.5, 0.2) - smeared against the car
// The Amiga picks them by the live scale, the same way it picks the hop phases.
constexpr int kHeroPhaseCount = kHeroSquashCount + 2;
struct Scale3 { double x, y, z; };
Scale3 heroScale(int phase)
{
    if (phase == kHeroSquashCount) return {1.7, 0.05, 1.7};
    if (phase == kHeroSquashCount + 1) return {1.0, 1.5, 0.2};
    return {1.0, kHeroPhases[phase], 1.0};
}
// 12 = four quarters with two intermediate frames each, as the user asked for after seeing the hero never turn.
constexpr int kHeroRotations = 12;

bool isHero(const std::string &name)
{
    // Only these two get animation frames - the user asked for no further character models, and nothing else in the
    // game animates (cars, logs and trains only translate; there is no wheel rotation anywhere in src/game).
    return name == "beaver" || name == "chicken";
}

// The other six playable characters exist in the shared data but the Amiga build does not ship them: the user asked
// for the beaver and the chicken and no further models. Skipping them took the set from 220 sprites/170 colours/
// 1.27 MB down to 172/102/422 KB.
bool isUnusedCharacter(const std::string &name)
{
    return name == "brent" || name == "avocoder" || name == "bacon" || name == "wheeler" || name == "palmer" ||
           name == "juwan";
}

// A row floor spans the whole map (25 x h x 1 units) and never rotates.
/* WHERE THE 3D RENDERER CUTS THIS MODEL OFF, in model space.
 *
 * Without a depth buffer the 3D renderer clips every non-floor object below the surface of the row it stands on
 * (src/game/scene_render.cpp: planeY = row.y + floorTop(floor), handed to the rasteriser as clipBelow/clipBelowY).
 * That is what hides a log's underwater half - and the baker, which renders each model alone with no row around
 * it, was shipping that half. Hence logs that sit ON the water instead of IN it, which is what the user saw.
 *
 * The plane is floorTop(row floor) - the object's own y in the row. Working it out for every kind:
 *
 *   log        water floor 0.125, log sits at -0.1  ->  0.225   <- the only one that cuts anything
 *   lily pad   water floor 0.125, pad sits at 0.125 ->  0.0
 *   car        road  floor 0.25,  car sits at 0.25  ->  0.0
 *   tree/rock  grass floor 0.375, sits at 0.4       -> -0.025
 *   train      rail  floor 0.25,  sits at 0.4       -> -0.15
 *
 * Every model starts at y = 0, so anything at or below zero removes nothing. Returning 0 therefore means "no
 * clipping", which is both true and cheap.
 */
double clipPlaneFor(const std::string &name)
{
    return name.compare(0, 4, "log_") == 0 ? 0.225 : 0.0;
}

bool isRowFloor(const std::string &name)
{
    return name == "grass_0" || name == "grass_1" || name == "road_0" || name == "road_1" || name == "river" ||
           name == "railroad";
}

struct Baked {
    std::string name;
    int rot = 0, phase = -1;
    int w = 0, h = 0, anchorX = 0, anchorY = 0;
};

// The game's camera, built exactly as SceneRenderer::setupCamera does (src/sw/scene_render_sw.cpp).
struct Camera {
    Mat4 view, projection, viewProj;
};

Camera makeCamera(mreal viewScale)
{
    Camera c;
    const Mat4 camWorld = lookAtRotation({-1, real(2.8), real(-2.9)}, {0, 0, 0}, {0, 1, 0});
    c.view = inverseRigid(camWorld); // camera at the origin: we bake objects relative to it
    const mreal w = mreal(gCanvasW) * viewScale, h = mreal(gCanvasH) * viewScale;
    c.projection = orthographic(-w, w, h, -h, settings::cameraNear, settings::cameraFar, settings::cameraZoom);
    // NOTE: SceneRenderer's viewShift is deliberately NOT applied. It slides the whole picture up or down; since a
    // sprite and its anchor come from the same matrix the shift cancels, and baking it in would freeze the framing.
    c.viewProj = c.projection * c.view;
    return c;
}

// The camera for one tile: same pixel density as the virtual canvas, the virtual origin (gCanvasW / 2) moved to where
// it lies relative to this tile's left edge.
Camera makeTileCamera(mreal viewScale, int tile)
{
    Camera c;
    const Mat4 camWorld = lookAtRotation({-1, real(2.8), real(-2.9)}, {0, 0, 0}, {0, 1, 0});
    c.view = inverseRigid(camWorld);
    const mreal w = mreal(gTileW) * viewScale, h = mreal(gCanvasH) * viewScale;
    c.projection = orthographic(-w, w, h, -h, settings::cameraNear, settings::cameraFar, settings::cameraZoom);
    const int shiftPx = (gCanvasW / 2 - tile * gTileW) - gTileW / 2;
    if (shiftPx != 0) {
        Mat4 shift = Mat4::identity();
        shift.e[12] = mreal(double(shiftPx) * 2.0 / double(gTileW));
        c.projection = shift * c.projection;
    }
    c.viewProj = c.projection * c.view;
    return c;
}

// Where a world point lands, in pixels of the canvas (top-left origin, like the rendered image).
void project(const Camera &cam, const Vec3 &p, double &px, double &py)
{
    const Vec3 ndc = cam.viewProj.transformPoint(p);
    px = (rd(ndc.x) * 0.5 + 0.5) * gCanvasW;
    py = (1.0 - (rd(ndc.y) * 0.5 + 0.5)) * gCanvasH;
}

// One render of one model at one rotation/scale, against a given clear colour.
void renderOnce(Renderer &renderer, const GpuMesh &mesh, const Camera &cam, double rotY, Scale3 scale,
                double clearR, double clearG, double clearB, std::vector<uint8_t> &rgba)
{
    renderer.bindTarget(nullptr);
    renderer.clear(real(clearR), real(clearG), real(clearB));
    renderer.setCamera(cam.projection, cam.view);
    renderer.setLightDirection({settings::lightX, settings::lightY, settings::lightZ});
    const Mat4 model = composeEuler({0, 0, 0}, {0, real(rotY), 0}, {real(scale.x), real(scale.y), real(scale.z)});
    renderer.drawLambert(mesh, GpuTexture(), model);
    renderer.readPixels(gTileW, gCanvasH, rgba);
}

// One render of the VIRTUAL canvas: every tile rendered and copied into place.
void renderVirtual(Renderer &renderer, const GpuMesh &mesh, const std::vector<Camera> &tiles, double rotY, Scale3 scale,
                   double clearR, double clearG, double clearB, std::vector<uint8_t> &out, std::vector<uint8_t> &tile)
{
    out.assign(size_t(gCanvasW) * gCanvasH * 4, 0);
    for (size_t t = 0; t < tiles.size(); t++) {
        renderOnce(renderer, mesh, tiles[t], rotY, scale, clearR, clearG, clearB, tile);
        const int x0 = int(t) * gTileW, cols = std::min(gTileW, gCanvasW - x0);
        for (int y = 0; y < gCanvasH; y++)
            std::memcpy(&out[(size_t(y) * gCanvasW + x0) * 4], &tile[size_t(y) * gTileW * 4], size_t(cols) * 4);
    }
}

// true where the two renders agree, i.e. where the model actually drew something
std::vector<uint8_t> opaqueMask(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
    std::vector<uint8_t> m(size_t(gCanvasW) * gCanvasH, 0);
    for (size_t i = 0; i < m.size(); i++) {
        const size_t p = i * 4;
        m[i] = (a[p] == b[p] && a[p + 1] == b[p + 1] && a[p + 2] == b[p + 2]) ? 1 : 0;
    }
    return m;
}

} // namespace

int main(int argc, char **argv)
{
    std::string data = "data_sf2000", out = "out/check/amiga/sprites", only;
    double viewScale = 6.0;
    bool calibrate = false, info = false;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
        else if (!std::strcmp(argv[i], "--view-scale") && i + 1 < argc) viewScale = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--canvas") && i + 2 < argc) { gCanvasW = std::atoi(argv[++i]); gCanvasH = std::atoi(argv[++i]); }
        else if (!std::strcmp(argv[i], "--calibrate")) calibrate = true;
        else if (!std::strcmp(argv[i], "--info")) info = true;
        else {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }
    data += "/";

    const Camera cam = makeCamera(real(viewScale));

    // CALIBRATION: the measurement that checks the arithmetic in docs/PLAN_AMIGA68K.md §4 instead of trusting it.
    if (calibrate) {
        double ox, oy, x1, y1, x2, y2, x3, y3;
        project(cam, {0, 0, 0}, ox, oy);
        project(cam, {1, 0, 0}, x1, y1);
        project(cam, {0, 1, 0}, x2, y2);
        project(cam, {0, 0, 1}, x3, y3);
        std::printf("calibration at view scale %.3f, canvas %dx%d\n", viewScale, gCanvasW, gCanvasH);
        std::printf("  origin (0,0,0) -> (%.4f, %.4f) px\n", ox, oy);
        std::printf("  +1 world X     -> (%+.4f, %+.4f) px\n", x1 - ox, y1 - oy);
        std::printf("  +1 world Y     -> (%+.4f, %+.4f) px\n", x2 - ox, y2 - oy);
        std::printf("  +1 world Z     -> (%+.4f, %+.4f) px\n", x3 - ox, y3 - oy);
        std::printf("  predicted by the plan: 200/viewScale = %.4f px per world unit along the camera axes\n",
                    200.0 / viewScale);
        std::printf("  row tilt (dy/dx along a row) = %+.4f\n", (y1 - oy) / (x1 - ox));
        return 0;
    }

    Manifest manifest;
    if (!loadManifest(data + "manifest.txt", manifest)) {
        std::fprintf(stderr, "no manifest in %s\n", data.c_str());
        return 2;
    }

    // INFO: how big each model actually is, in world units and in projected pixels - what tells a model that fits on
    // the canvas apart from one that is silently CLIPPED by it.
    if (info) {
        std::printf("%-22s %-28s %-22s %s\n", "model", "world size (x,y,z)", "world min y..max y", "projected bbox px");
        for (const auto &entry : manifest.models) {
            const std::string &name = entry.first;
            if (!only.empty() && name != only) continue;
            FlatMeshData fm;
            if (!loadFlatMesh(data + "meshes/" + name + ".fmesh", fm)) continue;
            double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
            for (int c = 0; c < 8; c++) {
                const Vec3 p{real((c & 1) ? fm.aabbMax[0] : fm.aabbMin[0]), real((c & 2) ? fm.aabbMax[1] : fm.aabbMin[1]),
                             real((c & 4) ? fm.aabbMax[2] : fm.aabbMin[2])};
                double px, py;
                project(cam, p, px, py);
                x0 = std::min(x0, px); x1 = std::max(x1, px);
                y0 = std::min(y0, py); y1 = std::max(y1, py);
            }
            char size[64], yr[64], box[80];
            std::snprintf(size, sizeof(size), "%.3f x %.3f x %.3f", fm.aabbMax[0] - fm.aabbMin[0],
                          fm.aabbMax[1] - fm.aabbMin[1], fm.aabbMax[2] - fm.aabbMin[2]);
            std::snprintf(yr, sizeof(yr), "%.3f .. %.3f", fm.aabbMin[1], fm.aabbMax[1]);
            std::snprintf(box, sizeof(box), "%.1f x %.1f%s", x1 - x0, y1 - y0,
                          (x1 - x0 > gCanvasW || y1 - y0 > gCanvasH) ? "   <-- LARGER THAN THE CANVAS" : "");
            std::printf("%-22s %-28s %-22s %s\n", name.c_str(), size, yr, box);
        }
        return 0;
    }

    gTileW = std::min(gCanvasW, kMaxTileW);
    std::vector<Camera> tiles;
    for (int t = 0; t * gTileW < gCanvasW; t++) tiles.push_back(makeTileCamera(real(viewScale), t));
    if (tiles.size() > 1) std::printf("canvas %dx%d rendered in %d tiles of %d\n", gCanvasW, gCanvasH, int(tiles.size()), gTileW);
    Renderer renderer;
    renderer.init(gTileW, gCanvasH);
    renderer.depthBuffer = true; // a single model must occlude itself correctly, whatever its triangle order

    std::vector<uint8_t> a(size_t(gCanvasW) * gCanvasH * 4), b(size_t(gCanvasW) * gCanvasH * 4), sprite, tileBuf;
    std::vector<Baked> baked;
    int files = 0, clipped = 0;

    for (const auto &entry : manifest.models) {
        const std::string &name = entry.first;
        if (!only.empty() && name != only) continue;
        if (isUnusedCharacter(name)) continue; // not shipped on the Amiga
        FlatMeshData fm;
        const std::string path = data + "meshes/" + name + ".fmesh";
        if (!loadFlatMesh(path, fm)) {
            std::fprintf(stderr, "cannot load %s\n", path.c_str());
            continue;
        }
        GpuMesh mesh = renderer.uploadMesh(fm);

        // THE HERO NEEDS MORE THAN FOUR. Its facing is a gsap tween in the shared logic
        // (src/game/player.cpp: gsap->to(&rotation(), {{'y', targetRotation}}, t)), so it turns CONTINUOUSLY -
        // four quarter turns cannot show a turn at all, they can only snap between two pictures. Twelve steps of
        // 30 degrees give two intermediate frames per quarter, which is what the turn actually looks like.
        // Everything else still gets four: cars and logs only ever face along an axis, and a tree's random
        // +-0.3 rad would round to zero at any step size.
        const int rotations = isRowFloor(name) ? 1 : (isHero(name) ? kHeroRotations : 4); // rows never rotate
        const int phases = isHero(name) ? kHeroPhaseCount : 1;
        for (int r = 0; r < rotations; r++) {
            const double rotY = r * (2.0 * PI / rotations);
            for (int p = 0; p < phases; p++) {
                // VEHICLES ARE BAKED 10% NARROWER ACROSS THE LANE (model z), nothing else: the user, playing, saw
                // cars and the train reach over onto the neighbouring rows. Length (x) and height (y) are untouched,
                // so speeds, gaps and collisions - which the game computes, not the picture - still match what is
                // seen along the lane.
                const bool vehicle = name.find("_car") != std::string::npos || name.find("_truck") != std::string::npos ||
                                     name == "taxi" || (name.compare(0, 6, "train_") == 0 && name.find("light") == std::string::npos);
                const Scale3 scaleY = isHero(name) ? heroScale(p) : Scale3{1.0, 1.0, vehicle ? 0.9 : 1.0};

                // CUT OFF WHAT THE WATER HIDES - IN THE RENDERER, where the height of every triangle is known.
                //
                // The first attempt did this in screen space, on the finished picture, reasoning that an
                // orthographic camera turns a horizontal world plane into a horizontal line on the canvas. That
                // is FALSE for this camera and the baker's own --calibrate says so: +1 world X moves a point by
                // (-31.51, -7.33) px, so the plane's image is tilted by 0.2325; worse, +1 world Z moves it by
                // (+10.87, -21.25), so the plane projects as a BAND spanned by two vectors, not a line. Measured
                // on log_3's own depth: a point of the same plane sits 5.94 px off that line at dz = 0.25 and
                // 11.89 px off at dz = 0.5. A pixel alone cannot say whether it is above or below water - the
                // depth is missing - and the result was a log sliced into a wedge, 128 px wide becoming 114.
                //
                // Renderer::drawClippedBelow does it properly: Sutherland-Hodgman on each triangle against
                // wy >= clip, in model space. It lives in the no-depth-buffer branch, so the clipped models are
                // baked the way the console ports draw the whole game - painter's order, front faces far to
                // near (paintOrder) - and only those models: everything else keeps the depth buffer it needs.
                // NOT named `clipped`: this function already has one, counting sprites that touch the canvas
                // edge, and shadowing it broke that counter's own increment.
                const double clipPlane = clipPlaneFor(name);
                const bool clipUnderwater = clipPlane > 0.0;
                if (clipUnderwater) {
                    renderer.depthBuffer = false;
                    renderer.clipBelow = true;
                    renderer.clipBelowY = real(clipPlane);
                }
                renderVirtual(renderer, mesh, tiles, rotY, scaleY, 1.0, 0.0, 1.0, a, tileBuf); // magenta
                renderVirtual(renderer, mesh, tiles, rotY, scaleY, 0.0, 1.0, 0.0, b, tileBuf); // green
                if (clipUnderwater) {
                    renderer.clipBelow = false;
                    renderer.depthBuffer = true;
                }
                const std::vector<uint8_t> mask = opaqueMask(a, b);

                int x0 = gCanvasW, y0 = gCanvasH, x1 = -1, y1 = -1;
                for (int y = 0; y < gCanvasH; y++)
                    for (int x = 0; x < gCanvasW; x++) {
                        if (!mask[size_t(y) * gCanvasW + x]) continue;
                        if (x < x0) x0 = x;
                        if (y < y0) y0 = y;
                        if (x > x1) x1 = x;
                        if (y > y1) y1 = y;
                    }
                if (x1 < x0 || y1 < y0) {
                    std::fprintf(stderr, "%s rot %d phase %d: nothing drawn\n", name.c_str(), r, p);
                    continue;
                }
                // Touching the canvas edge means the model did not fit and has been silently truncated. This check is
                // what caught the row floors being cut off at a 256 px canvas.
                if (x0 == 0 || y0 == 0 || x1 == gCanvasW - 1 || y1 == gCanvasH - 1) {
                    std::fprintf(stderr, "CLIPPED: %s rot %d touches the canvas edge (%dx%d at %d,%d)\n", name.c_str(),
                                 r, x1 - x0 + 1, y1 - y0 + 1, x0, y0);
                    clipped++;
                }

                const int w = x1 - x0 + 1, h = y1 - y0 + 1;
                sprite.assign(size_t(w) * h * 4, 0);
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++) {
                        const size_t src = size_t(y + y0) * gCanvasW + (x + x0);
                        if (!mask[src]) continue; // stays transparent
                        const size_t s4 = src * 4, dst = (size_t(y) * w + x) * 4;
                        sprite[dst] = a[s4];
                        sprite[dst + 1] = a[s4 + 1];
                        sprite[dst + 2] = a[s4 + 2];
                        sprite[dst + 3] = 255;
                    }

                double ox, oy;
                project(cam, {0, 0, 0}, ox, oy);
                Baked rec;
                rec.name = name;
                rec.rot = r;
                rec.phase = isHero(name) ? p : -1;
                rec.w = w;
                rec.h = h;
                // Anchor: where the model's own origin sits inside the cropped sprite. The Amiga projects an object's
                // world position to a pixel and subtracts this, so the sprite lands where the 3D version would.
                rec.anchorX = int(std::lround(ox)) - x0;
                rec.anchorY = int(std::lround(oy)) - y0;

                char file[512];
                if (rec.phase >= 0)
                    std::snprintf(file, sizeof(file), "%s/%s_r%d_s%d.png", out.c_str(), name.c_str(), r, p);
                else
                    std::snprintf(file, sizeof(file), "%s/%s_r%d.png", out.c_str(), name.c_str(), r);
                if (!png::writeRGBA(file, w, h, sprite.data())) {
                    std::fprintf(stderr, "cannot write %s (does %s exist?)\n", file, out.c_str());
                    return 3;
                }
                baked.push_back(rec);
                files++;
            }
        }
        renderer.releaseMesh(mesh);
    }

    const std::string indexPath = out + "/sprites.txt";
    FILE *f = std::fopen(indexPath.c_str(), "w");
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", indexPath.c_str());
        return 3;
    }
    std::fprintf(f, "# name rot phase w h anchorX anchorY   (view scale %.3f, canvas %dx%d)\n", viewScale, gCanvasW,
                 gCanvasH);
    for (const Baked &r : baked)
        std::fprintf(f, "%s %d %d %d %d %d %d\n", r.name.c_str(), r.rot, r.phase, r.w, r.h, r.anchorX, r.anchorY);
    std::fclose(f);

    std::printf("sw_bake_amiga: %d sprites -> %s%s\n", files, out.c_str(),
                clipped ? "   *** SOME WERE CLIPPED, see above ***" : "");
    return clipped ? 4 : 0;
}
