// The Amiga game: shared logic, sprites instead of polygons (tasks F1 + D3 + E3).
//
// The whole port rests on one property of the original: the camera is ORTHOGRAPHIC and never rotates, and
// Game::forwardScene slides the WORLD rather than the camera. So an object's size on screen never changes, one baked
// picture per model is correct everywhere, and placing it costs a linear transform - no perspective divide, no
// scaling, no depth buffer.
//
// What is shared with SF2000/R36S: everything under src/game (movement, collisions, map generation, scoring), built
// with CR_FIXED so every number is 16.16 - no float or double reaches this CPU, which has no FPU.
// What is ours: this file, the blitter, the sprite container, the sound player and the platform layer.
//
// NO AMIGA <proto/*> HEADERS IN THIS FILE. That rule is inherited from the OpenTTD port ("that collision is what
// sank the previous attempt") and this file proved it again: including <proto/dos.h> made `Node` stop resolving to
// the game's scene node and made `Input` ambiguous against AmigaDOS's BPTR Input(VOID). Everything the Amiga side
// needs arrives through headers that are deliberately free of Amiga types.
//
// Deliberately NOT used here: std::sort. The toolchain's inliner segfaults inside bits/stl_heap.h on this target, so
// the painter's order uses an insertion sort over a few dozen items - which is also the right algorithm for a list
// that is nearly sorted every frame.
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "engine/assets.h"
#include "engine/input.h"
#include "engine/math.h"
#include "engine/renderer.h" // src/amiga/shim/engine - the overlay calls of the shared screens, on the blitter
#include "engine/text.h"     // likewise
#include "game/game.h"
#include "game/models.h"
#include "game/scene.h"
#include "game/settings.h"
#include "game/smoke_bot.h" // the same bot the console builds play with - see AutoPlay
#include "game/sound_volume.h"
#include "ui/hud.h"
#include "ui/lang.h"
#include "ui/screens.h" // THE SHARED SCREENS: banners, pause, settings, career, ranks - unchanged

extern "C" {
#include "amiga_gfx.h"
#include "audio_bh.h"
#include "blit.h"
#include "clock_bh.h"
#include "prefs_bh.h"
#include "version_bh.h"
#include "joy_bh.h"
#include "music_bh.h"
#include "sprite_ids.h" // the generated name table: included by exactly this one translation unit
#include "sprites.h"
}

#include <stdio.h>

using namespace cr;

namespace cr {
// C++17 makes a static constexpr member implicitly inline; C++14, which is as far as this toolchain goes, does not.
// Game::kDt is ODR-used inside the shared logic, so exactly one translation unit must define it - and it should be
// one of OURS, so that nothing in src/game has to change for the Amiga.
constexpr real Game::kDt;
} // namespace cr

namespace {

// Profile stamps in MICROSECONDS (timer.device EClock). The DateStamp clock the game paces itself with ticks every
// 20 ms - it cannot tell a 10 ms c2p from a 16 ms one. Wraps after 71 minutes; only differences are ever used.
// PROFILING AND DUMPS ONLY IN A TEST RUN (autoplay.txt, compare mode, or PROGDIR:profile.txt). In a normal game
// they were writing to the disk the music streams from: a line every five seconds, and a 77 KB screen dump at every
// single death - the user felt it as the game catching now and then. Off, the stamps below cost a branch.
bool gProfiling = false;
inline unsigned long profMicros() { return gProfiling ? (unsigned long)bh_micros() : 0UL; }

// THE SCREEN SIZE IS A SETTING NOW (BobrHopperPrefs): 320x240 on AGA and RTG, 640x480 on RTG only. Both show the
// SAME piece of the world - the 640 sprites are baked at twice the size (tools: sw_bake_amiga --view-scale 3) - so
// the framing stays the consoles' and only the pixels per world unit double. Widths are multiples of 32: both c2p
// kernels work in 32-pixel columns.
int gScreenW = 320, gScreenH = 240;
int gPixelScale = 1;                          // 1 at 320x240, 2 at 640x480: every pixel constant below is times this
inline int viewScaleFor(int screenW) { return 6 * 320 / screenW; } // 6 at 320 (the SF2000 framing), 3 at 640
constexpr int kSfxVolume = 48; // Paula's own scale is 0..64

// Amiga raw key codes (RAWKEY): bit 7 set means the key came up.
constexpr int kRawUp = 0x4C, kRawDown = 0x4D, kRawRight = 0x4E, kRawLeft = 0x4F;
constexpr int kRawEsc = 0x45;

// Which sprites belong to one model. Built once at startup from the generated table, so no string work happens
// while the game is running.
struct SpriteSet {
    short rot[BH_ROT_MAX];
    short phase[BH_ROT_MAX][BH_HERO_PHASE_COUNT];
    // How many directions THIS model was baked at: 1 for a row strip, 4 for cars, logs and trees, 12 for the
    // hero. It has to be per model, because the angle is divided into that many sectors when choosing a sprite.
    short rotCount;
    bool hero;
    // WHICH PASS: 0 an object, 1 a row's floor, 2 something that LIES ON the floor and can never hide anything -
    // a log or a lily pad - and is therefore drawn straight after its own water, before any nearer floor.
    char layer;
    SpriteSet() : rotCount(1), hero(false), layer(0)
    {
        for (int r = 0; r < BH_ROT_MAX; r++) {
            rot[r] = -1;
            for (int p = 0; p < BH_HERO_PHASE_COUNT; p++) phase[r][p] = -1;
        }
    }
};

// One thing to draw this frame.
// ONE LIST, SORTED BY ROW, THEN LAYER, THEN DEPTH - the user's model, and the right one:
//   for every row, farthest first:  the ground -> what lies on it (logs, lily pads, the finish squares, a run-over
//   hero) -> what stands on it, far to near.
// Every object in the game is a child of its row, so nothing has to be guessed; objects rise UP the screen, towards
// rows that are already painted, so a nearer row never wrongly covers one. It replaces two earlier schemes that each
// fixed one overlap and caused another (one global depth sort: the road swallowed its own car; floors-then-objects:
// a far log painted over the near grass).
struct Item {
    int32_t far; // distance along the view; larger is farther away
    short id;    // sprite, or -1 for a flat-coloured shape
    short x, y;  // where the origin lands, in pixels
    short row;   // world z of the row it belongs to; larger is farther
    signed char layer; // 0 ground, 1 lying on it, 2 standing on it
    unsigned char colour; // shapes only
    short clipY;          // sprites: draw nothing at or below this screen line (0 = no clip) - the drowning hero
    uint32_t key;         // row, layer and depth folded into one number, SMALLER = painted earlier (see sortItems)
    short qx[4], qy[4];   // shapes: the quad's corners
};

class AmigaRenderer {
public:
    bool init(const BHSprites *sprites, const ModelLibrary &models, int viewW, int viewH)
    {
        sprites_ = sprites;
        // The REAL drawable size, not the screen size. With the Intuition bar visible the game area is shorter
        // than 240 lines, and a projection built for 240 would push the whole scene off the bottom - the same
        // class of mistake that cost four wrong hypotheses earlier today.
        viewW_ = viewW;
        viewH_ = viewH;

        // The camera, built exactly as the sprite baker and SceneRenderer::setupCamera build it. Deriving it here
        // rather than hard-coding the measured pixel constants means the sprites and their placement can never
        // drift apart: both come from the same matrices.
        // NAMED LOCALS, PARENTHESISED CONSTRUCTION - NOT brace-initialised temporaries.
        //
        // Written as lookAtRotation({-1, real(2.8), real(-2.9)}, {0, 0, 0}, {0, 1, 0}) - the way the shared
        // renderers write it - this produced an ALL-ZERO view matrix on this target, and every symptom followed
        // from that: each object projected to ndc (0,0) and the whole scene piled up at screen centre (160,120).
        //
        // It is not the maths. A probe measured dot, length, normalize, cross and rsqrt on this machine and all
        // were correct to a few raw 16.16 units, so the shared vector code is sound here. What fails is the
        // brace-initialised Vec3 temporaries carrying mixed int/Fixed literals into the call: with those arriving
        // as (0,0,0), lookAtRotation's own zero-length guard turns z into (0,0,1), cross(up, z) is zero, and the
        // rotation collapses - which matches the dumped matrix element for element.
        //
        // Treat brace-initialised Vec3 as a live suspect anywhere else in this build (gcc 6.5.0b, m68k, -O1).
        // THE CAMERA BASIS IS BUILT HERE, STEP BY STEP, INSTEAD OF CALLING lookAtRotation().
        //
        // Why: lookAtRotation() returns a broken matrix on this target. Dumped, its columns were
        //   x = (0,0,0), y = (0,0,0), z = (-1.0, 2.8, -2.9)
        // - the z column is the RAW eye vector, never normalised, and normalize() only returns its input
        // unchanged when it sees a length of zero. Yet a probe calling dot/length/normalize/cross directly, in
        // this same file, got all of them right to a few raw 16.16 units. So the maths is sound and the INLINED
        // function is miscompiled: gcc 6.5.0b on m68k at -O1, which has already produced two internal compiler
        // errors on this project.
        //
        // Doing it as named locals, one operation per statement, is the pattern the probe proved works. It also
        // keeps full precision rather than hard-coding the measured basis as constants.
        // THE BASIS IS A TABLE OF CONSTANTS, because rsqrt() IS BROKEN ON THIS TARGET.
        //
        // Measured, not assumed: a runtime probe (values passed through volatile, so the compiler could not fold
        // them) returned rsqrt(17.25) = 0 AND rsqrt(4.0) = 0. Every input, not just awkward ones. fixedSqrt starts
        // with `uint64_t bit = 1 << 46` and then `while (bit > n) bit >>= 2;` - if that 64-bit shift or compare
        // misbehaves here, bit is zero, the loop never runs and the function returns zero, which is precisely what
        // it does. normalize() calls it, so the camera basis collapsed and the whole scene piled up at (160,120).
        //
        // The blast radius is only this: the shared GAME LOGIC never calls rsqrt/normalize/length (the only uses
        // are in scene_render.cpp, the GLES renderer, which the Amiga does not build). So play is unaffected, and
        // the proper repair of fixedSqrt belongs in shared code with SF2000/R36S regressions behind it - task E4.
        //
        // These are the same vectors the sprite baker used, so sprites and placement still come from one source of
        // truth: normalize(-1, 2.8, -2.9) and the two crosses, evaluated exactly, in 16.16.
        const Vec3 xAxis(Fixed::fromRaw(-61949), Fixed::fromRaw(0), Fixed::fromRaw(21365));
        const Vec3 yAxis(Fixed::fromRaw(14402), Fixed::fromRaw(48404), Fixed::fromRaw(41764));
        const Vec3 zAxis(Fixed::fromRaw(-15781), Fixed::fromRaw(44184), Fixed::fromRaw(-45759));
        // One datapoint kept for the record: a 64-bit shift by 46, which is the first thing fixedSqrt does.
        {
            volatile int shiftBits = 46;
            const unsigned long long shifted = 1ULL << shiftBits;
            printf("probe64: 1<<46 = %lu:%lu (expect 16384:0)\n", (unsigned long)(shifted >> 32),
                   (unsigned long)(shifted & 0xffffffffULL));
        }
        printf("basis: x=(%ld,%ld,%ld)  y=(%ld,%ld,%ld)  z=(%ld,%ld,%ld)\n", (long)xAxis.x.v, (long)xAxis.y.v,
               (long)xAxis.z.v, (long)yAxis.x.v, (long)yAxis.y.v, (long)yAxis.z.v, (long)zAxis.x.v, (long)zAxis.y.v,
               (long)zAxis.z.v);

        // The view matrix is the transpose of [x y z] (the inverse of a rotation), written as nine assignments:
        // inverseRigid() does it with a nested loop over computed indices and also came back all zeros here.
        Mat4 inv = Mat4::identity();
        inv.e[0] = xAxis.x;
        inv.e[4] = xAxis.y;
        inv.e[8] = xAxis.z;
        inv.e[1] = yAxis.x;
        inv.e[5] = yAxis.y;
        inv.e[9] = yAxis.z;
        inv.e[2] = zAxis.x;
        inv.e[6] = zAxis.y;
        inv.e[10] = zAxis.z;
        viewRel_ = inv;
        const mreal w = mreal(viewW_) * mreal(viewScaleFor(gScreenW)), h = mreal(viewH_) * mreal(viewScaleFor(gScreenW));
        Mat4 proj = orthographic(-w, w, h, -h, settings::cameraNear, settings::cameraFar, settings::cameraZoom);

        // The same vertical framing as SF2000 and R36S. Those raise the picture by viewShift = -0.15 NDC so the
        // hero sits higher in frame (heroY 0.74 rather than 0.82); without it the Amiga framed the game lower
        // than the other two ports, which a pixel comparison measured as an eight-fold difference that had
        // nothing to do with drawing. The sprite baker deliberately omits the shift - a sprite and its anchor
        // come from the same matrix, so it cancels there - which is exactly why it has to be applied HERE.
        {
            Mat4 shift = Mat4::identity();
            shift.e[13] = -mreal(settings::defaultViewShift);
            proj = shift * proj;
        }
        viewProj_ = proj * viewRel_;

        // Is the projection built wrong, or clobbered later? One run separates the two. A CONTROL comes first: the
        // same function at tiny magnitudes, where e[0] must be exactly 1.0 (65536 in 16.16). If the control is right
        // and the real one is zero, 16.16 overflowed on the way - the real extents are +-1920 with zoom 400.
        const Mat4 control = orthographic(mreal(-1), mreal(1), mreal(1), mreal(-1), mreal(-30), mreal(30), mreal(1));
        printf("proj: control e0=%ld (expect 65536) e5=%ld\n", (long)control.e[0].v, (long)control.e[5].v);
        printf("proj: w=%ld h=%ld near=%ld far=%ld zoom=%ld (16.16)\n", (long)w.v, (long)h.v,
               (long)mreal(settings::cameraNear).v, (long)mreal(settings::cameraFar).v,
               (long)mreal(settings::cameraZoom).v);
        printf("proj: real e0=%ld e5=%ld e10=%ld e12=%ld e13=%ld e15=%ld\n", (long)proj.e[0].v, (long)proj.e[5].v,
               (long)proj.e[10].v, (long)proj.e[12].v, (long)proj.e[13].v, (long)proj.e[15].v);
        printf("view: e0=%ld e4=%ld e8=%ld | e1=%ld e5=%ld e9=%ld\n", (long)viewRel_.e[0].v, (long)viewRel_.e[4].v,
               (long)viewRel_.e[8].v, (long)viewRel_.e[1].v, (long)viewRel_.e[5].v, (long)viewRel_.e[9].v);
        printf("viewProj at init: row0=(%ld,%ld,%ld,%ld)\n", (long)viewProj_.e[0].v, (long)viewProj_.e[4].v,
               (long)viewProj_.e[8].v, (long)viewProj_.e[12].v);

        // The baked directions, built with the SAME function the baker used (composeEuler), so the renderer's
        // idea of "direction k" cannot drift from the sprite that was baked for it. Printed once: on this
        // machine a maths function returning silent zeros has already cost a day (rsqrt), so a table that
        // decides every hero frame is worth one line in the log.
        for (int k = 0; k < BH_ROT_MAX; k++) {
            const real angle = real(6.283185307179586) * real(k) / real(BH_ROT_MAX);
            rotBasis_[k] = composeEuler(Vec3(real(0), real(0), real(0)), Vec3(real(0), angle, real(0)),
                                        Vec3(real(1), real(1), real(1)));
        }
        printf("rotations: %d baked directions, basis x =", BH_ROT_MAX);
        for (int k = 0; k < BH_ROT_MAX; k++)
            printf(" (%ld,%ld)", (long)rotBasis_[k].e[0].v, (long)rotBasis_[k].e[2].v);
        printf("\n");

        // Map every model to its sprites, by name, once.
        for (std::map<std::string, Model>::const_iterator it = models.models.begin(); it != models.models.end(); ++it) {
            SpriteSet set;
            for (int i = 0; i < BH_SPRITE_NAME_COUNT; i++) {
                const BHSpriteName &n = bh_sprite_names[i];
                if (it->first != n.name) continue;
                if (n.rot >= 0 && n.rot + 1 > set.rotCount) set.rotCount = short(n.rot + 1);
                if (n.phase < 0) {
                    if (n.rot >= 0 && n.rot < BH_ROT_MAX) set.rot[n.rot] = n.id;
                } else {
                    set.hero = true;
                    if (n.rot >= 0 && n.rot < BH_ROT_MAX && n.phase < BH_HERO_PHASE_COUNT)
                        set.phase[n.rot][n.phase] = n.id;
                }
            }
            if (set.rot[0] < 0 && !set.hero) continue; // a model this build does not ship (the unused characters)
            if (it->second.receiveShadow && !it->second.castShadow) set.layer = 1;
            else if (it->first.compare(0, 3, "log") == 0 || it->first == "lily_pad") set.layer = 2;
            sets_[&it->second] = set;
        }
        // THE MODEL CARRIES ITS OWN SPRITE SET. Model::mesh is "the renderer's handle for this model" and is null
        // in every headless build, this one included - so the renderer that exists here uses it for exactly that.
        // It replaces a std::map lookup per node per frame with one pointer read. (std::map nodes never move.)
        for (std::map<const Model *, SpriteSet>::iterator it = sets_.begin(); it != sets_.end(); ++it)
            const_cast<Model *>(it->first)->mesh = reinterpret_cast<const GpuMesh *>(&it->second);

        // THE PROJECTION AS PLAIN 32-BIT INTEGERS. The camera never turns and never zooms, so screen x, screen y
        // and the painter's depth are each a fixed linear function of the position: three multiplies apiece. The
        // generic path did this through two 4x4 matrices in 16.16, where every multiply is a 64-bit product - 18
        // of them per node - on a CPU family whose 060 does not even have that instruction.
        //   position relative to the camera, 8 fractional bits  (|p| < 64 units -> < 2^14)
        //   coefficient in pixels per unit, 10 fractional bits   (|k| < 34 px    -> < 2^16)
        //   product: 18 fractional bits, < 2^30 - fits, with room for the sum of three.
        // The position loses 8 bits: 1/256 of a unit is 0.13 of a pixel.
        // At 640x480 a unit is twice as many pixels, so the coefficient keeps ONE bit less (9) and the product has
        // 17 fractional bits - the same headroom, the same 32-bit multiply.
        {
            projShift_ = gPixelScale > 1 ? 17 : 18;
            const int kShift = 24 - projShift_; // 16.16 -> (projShift_ - 8) fractional bits
            const real halfW = real(viewW_) * real(0.5), halfH = real(viewH_) * real(0.5);
            for (int c = 0; c < 3; c++) {
                kx_[c] = (viewProj_.e[c * 4 + 0] * halfW).v >> kShift;
                ky_[c] = -((viewProj_.e[c * 4 + 1] * halfH).v >> kShift);
                kz_[c] = viewRel_.e[c * 4 + 2].v >> 6; // the painter's key: independent of the screen
            }
            // + half a pixel: the shift then rounds
            ox_ = ((viewProj_.e[12] * halfW + halfW).v << (projShift_ - 16)) + (1L << (projShift_ - 1));
            oy_ = ((halfH - viewProj_.e[13] * halfH).v << (projShift_ - 16)) + (1L << (projShift_ - 1));
            printf("projection: kx=(%ld,%ld,%ld) ky=(%ld,%ld,%ld) kz=(%ld,%ld,%ld) /2^10 px per unit\n", (long)kx_[0],
                   (long)kx_[1], (long)kx_[2], (long)ky_[0], (long)ky_[1], (long)ky_[2], (long)kz_[0], (long)kz_[1],
                   (long)kz_[2]);
        }
        printf("renderer: %d models mapped to sprites\n", (int)sets_.size());
        return !sets_.empty();
    }

    void render(BHSurface &surface, Game &game)
    {
        items_.clear();
        floorsDrawn_ = 0;
        culled_ = 0;
        if (census_) { // the census describes ONE frame - the dumped one - so it starts empty every frame
            seen_.clear();
            drawn_.clear();
            culledBy_.clear();
            unmapped_.clear();
            noid_.clear();
        }
        origin_ = game.cameraPosition();
        const unsigned long t0 = profMicros();
        const unsigned long t1 = profMicros();
        {
            // THE HERO'S ROW. Between two rows he belongs to the NEARER one, or the near row's ground would be painted
            // over his feet. Once dead he stays in the row he died in - the game moves a body about (pushed to the
            // road's edge, carried by a car, sunk) and re-deriving the row from that made the corpse jump a line.
            const Player &hero = game.hero();
            heroNode_ = hero.object;
            heroDead_ = !hero.isAlive;
            // ROWS ARE COUNTED IN THE WORLD'S OWN SPACE, not the camera's. The world group slides continuously as the
            // camera eases after the hero, so a position rounded in the slid space changes row as it slides: the
            // finish line's squares sit at z = row +- 0.25 and hopped between two rows - and so between being
            // painted over and not - every few frames. The user saw it flicker.
            worldZ_ = 0;
            for (const cr::Node *n = hero.object->parent; n; n = n->parent) worldZ_ += long(n->position.z.v);
        }
        walk(game.sceneRoot(), 0, Vec3(real(0), real(0), real(0)), kNoRow);
        const unsigned long t2 = profMicros();
        sortItems(items_);
        const unsigned long t3 = profMicros();
        profWorld_ += t1 - t0;
        profCollect_ += t2 - t1;
        profSort_ += t3 - t2;

        // THE SKY IS ONLY PAINTED WHEN IT CAN BE SEEN. In play the ground strips - 798 pixels wide, a dozen of them -
        // cover every pixel, and wiping 73 KB first was 2.8 ms a frame thrown away. With few strips in view (the
        // edge of the world, a scene being rebuilt) the wipe stays.
        //
        // PROVEN EVERY FRAME, NOT ASSUMED: a sparse grid of sentinels (index 0, which no sprite ever writes) goes
        // down first; any that survives the frame means sky was showing there, and the wipe comes back for a
        // while. The cost is 192 byte writes and 192 reads.
        const bool wipe = needClear_ > 0;
        if (wipe) {
            bh_clear(&surface, BH_SKY_INDEX);
            needClear_--;
        } else {
            for (int gy = 0; gy < 12; gy++)
                for (int gx = 0; gx < 16; gx++)
                    surface.pixels[(unsigned long)(gy * (surface.height - 1) / 11) * (unsigned long)surface.pitch +
                                   (unsigned long)(gx * (surface.width - 1) / 15)] = 0;
        }
        const unsigned long tc = profMicros();
        profClear_ += tc - t3;
        for (size_t i = 0; i < items_.size(); i++) {
            const Item &it = items_[order_[i]];
            const unsigned long ti = profMicros();
            struct Tally { unsigned long &into, from; Tally(unsigned long &i, unsigned long f) : into(i), from(f) {} ~Tally() { into += profMicros() - from; } }
                tally(it.layer == 0 ? profFloors_ : profObjects_, ti);
            if (it.id < 0) {
                fillQuad(surface, it.qx, it.qy, it.colour);
            } else if (it.clipY > 0) {
                BHSurface cut = surface; // same pixels, shorter: the blitter's own clip does the rest
                if (it.clipY < cut.height) cut.height = it.clipY;
                bh_blit_at_anchor(&cut, sprites_, it.id, it.x, it.y);
            } else {
                bh_blit_at_anchor(&surface, sprites_, it.id, it.x, it.y);
            }
        }
        if (!wipe) {
            for (int gy = 0; gy < 12; gy++)
                for (int gx = 0; gx < 16; gx++) {
                    unsigned char &px = surface.pixels[(unsigned long)(gy * (surface.height - 1) / 11) * (unsigned long)surface.pitch +
                                                       (unsigned long)(gx * (surface.width - 1) / 15)];
                    if (px == 0) {
                        px = BH_SKY_INDEX;
                        needClear_ = 60;
                    }
                }
        }
        profBlit_ += profMicros() - t3;
    }

    unsigned long profWorld_ = 0, profCollect_ = 0, profSort_ = 0, profBlit_ = 0, profNodes_ = 0;
    unsigned long profClear_ = 0, profFloors_ = 0, profObjects_ = 0;
    void profReport()
    {
        printf("profile/render: updateWorld %lu ms, collect %lu, sort %lu, clear+blit %lu, nodes walked %lu\n",
               profWorld_ / 1000UL, profCollect_ / 1000UL, profSort_ / 1000UL, profBlit_ / 1000UL, profNodes_);
        printf("profile/draw: clear %lu ms, floors %lu, everything else %lu\n", profClear_ / 1000UL, profFloors_ / 1000UL,
               profObjects_ / 1000UL);
        profClear_ = profFloors_ = profObjects_ = 0;
        profWorld_ = profCollect_ = profSort_ = profBlit_ = profNodes_ = 0;
    }

    size_t drawn() const { return items_.size(); }

    void enableCensus() { census_ = true; }

    void setDetail(bool on) { detail_ = on; }

    /* Census on for ONE frame. Counting by model name costs a std::string map lookup per node, so it may not
     * run during play - but a playtest that never counts is how a renderer with missing roads passed. */
    void setCensus(bool on) { census_ = on; }

    /* Did this frame actually draw any of `name`? The census counts the whole POOL - the game keeps 20 rows of
     * every type alive whether or not they are anywhere near the screen - so "seen 20, drawn 0" is not evidence
     * of anything being broken. Twice I read it as a missing feature; twice the objects turned out to be parked
     * at world z = -38 and hundreds of pixels below the window. This lets a playtest dump a frame WHEN the thing
     * is really on screen, instead of hoping to catch one. */
    int drawnCount(const char *name) const
    {
        std::map<std::string, int>::const_iterator it = drawn_.find(std::string(name));
        return it == drawn_.end() ? 0 : it->second;
    }

    void printCensus() const
    {
        for (std::map<std::string, int>::const_iterator it = seen_.begin(); it != seen_.end(); ++it) {
            const std::string &n = it->first;
            printf("census: %-22s seen %3d drawn %3d culled %3d unmapped %3d nosprite %3d\n", n.c_str(), it->second,
                   censusValue(drawn_, n), censusValue(culledBy_, n), censusValue(unmapped_, n),
                   censusValue(noid_, n));
        }
    }
    int culled_ = 0; // objects skipped this frame because none of their sprite could land on screen
    // The REAL drawable area. With the Intuition bar visible it is shorter than the screen, and a projection
    // built for the full height would push the scene off the bottom.
    int viewW_ = 320, viewH_ = 240;
    int projShift_ = 18; // fractional bits of a projected coordinate - see init()

private:
    /* ONE WALK, NO MATRICES UNLESS A TURNED NODE HAS CHILDREN, AND NOTHING COMPUTED FOR WHAT CANNOT BE SEEN.
     *
     * History, all measured on an honest 68040 (no JIT, -80%):
     *   - scene::updateWorld() + collect() cost 150 ms a frame: a full Euler matrix - sines, cosines, a 4x4
     *     product - for every node in the graph, hidden pool rows included.
     *   - the first rewrite still spent 25 ms: a std::map lookup per node, four dot products per tree to choose a
     *     direction the tree never changes, 18 64-bit multiplies to project it, and only THEN the test that threw
     *     85% of that work away.
     *
     * What a sprite needs is where its origin lands and which way it faces:
     *   - the origin is the parent's position plus its own while the chain above is pure translation. A node's OWN
     *     rotation and scale do not move its own origin, so a turned tree or car costs three additions too;
     *   - the direction comes straight from rotation.y - one integer division, no trigonometry;
     *   - a matrix is built only for a turned or scaled node that HAS CHILDREN (the hero's group), for their sake.
     */
    void walk(cr::Node *node, const Mat4 *parentWorld, const Vec3 &parentPos, int row)
    {
        if (!node || !node->visible) return;
        profNodes_++;
        const bool hasChildren = !node->children.empty();
        const bool plain = node->rotation.x == real(0) && node->rotation.y == real(0) && node->rotation.z == real(0) &&
                           node->scale.x == real(1) && node->scale.y == real(1) && node->scale.z == real(1);
        Vec3 pos;
        const Mat4 *mine = 0;
        int rotFromMatrix = -1;
        if (!parentWorld) {
            pos = Vec3(parentPos.x + node->position.x, parentPos.y + node->position.y, parentPos.z + node->position.z);
            if (!plain && hasChildren) {
                node->world = node->localMatrix();
                node->world.e[12] = pos.x;
                node->world.e[13] = pos.y;
                node->world.e[14] = pos.z;
                mine = &node->world;
            }
        } else {
            node->world = mulAffine(*parentWorld, node->localMatrix());
            pos = Vec3(node->world.e[12], node->world.e[13], node->world.e[14]);
            mine = &node->world;
            rotFromMatrix = 1;
        }

        // Relative to the camera, 8 fractional bits - see the projection note in init().
        const long rx = (pos.x.v - origin_.x.v) >> 8, ry = (pos.y.v - origin_.y.v) >> 8, rz = (pos.z.v - origin_.z.v) >> 8;

        // A WHOLE ROW AT ONCE, BEFORE ITS CHILDREN ARE TOUCHED. Only a row is tested: a model-less group whose
        // first child carries a floor model (src/game/rows.cpp builds every row that way) - the scene root is a
        // model-less group too, and cutting IT by where its origin lands takes the world with it. In SCREEN space:
        // the camera does not stand over the middle of the picture, and testing world z against it once left the
        // user looking at a bare sky ten rows in. A row's children spread +-10 units along x (+-73 px of height),
        // the tallest sprite rises ~120 px above its anchor and hangs ~20 below it.
        if (!node->model && hasChildren && !parentWorld) {
            const Model *first = node->children[0]->model;
            if (first && first->receiveShadow && !first->castShadow) {
                const int gy = int((oy_ + ky_[0] * rx + ky_[1] * ry + ky_[2] * rz) >> projShift_);
                if (gy < -100 * gPixelScale || gy > viewH_ + 200 * gPixelScale) return;
                row = int((pos.z.v - worldZ_ + 32768) >> 16); // everything beneath belongs to this row
            }
        }
        if (node == heroNode_) {
            if (!heroDead_) heroRow_ = int((pos.z.v - worldZ_ + 3277) >> 16); // floor(z + 0.05), in the same space as the rows
            row = heroRow_;
        }
        // Not under a row and not the hero - particles, the finish squares: the row they are standing in.
        const int myRow = row != kNoRow ? row : int((pos.z.v - worldZ_ + 32768) >> 16);

        if (node->model) {
            if (census_) seen_[node->model->name]++;
            const SpriteSet *set = reinterpret_cast<const SpriteSet *>(node->model->mesh);
            if (set) add(node, *set, rx, ry, rz, rotFromMatrix, myRow);
            else if (census_) unmapped_[node->model->name]++;
        } else if (node->shape != Shape::None) {
            addShape(node, rx, ry, rz, myRow);
        }
        const size_t n = node->children.size();
        for (size_t i = 0; i < n; i++) walk(node->children[i], mine, pos, row);
    }

    // THE GAME'S PROCEDURAL GEOMETRY: flat-coloured planes and boxes with no model - the FINISH LINE of a
    // Progression level (36 black and white squares lying on the grass), the feathers and the splash of a death,
    // the foam. The renderer skipped every node without a model, so none of it was ever drawn: that is why a
    // level had no finish line and a death had no feathers.
    void addShape(cr::Node *node, long rx, long ry, long rz, int row)
    {
        const long cx = ox_ + kx_[0] * rx + kx_[1] * ry + kx_[2] * rz, cy = oy_ + ky_[0] * rx + ky_[1] * ry + ky_[2] * rz;
        const int sx = int(cx >> projShift_), sy = int(cy >> projShift_);
        const int m = 40 * gPixelScale;
        if (sx < -m || sx > viewW_ + m || sy < -m || sy > viewH_ + m) return;
        Item item;
        item.id = -1;
        item.x = short(sx);
        item.y = short(sy);
        item.row = short(row);
        item.clipY = 0;
        item.far = int32_t(-(kz_[0] * rx + kz_[1] * ry + kz_[2] * rz));
        item.colour = artColour(node->shapeColor);
        if (node->shape == Shape::Plane) {
            // Lying flat (rotation.x = 90 degrees): its width runs along world x and its height along world z.
            // Half extents with 8 fractional bits, to match the projection's units.
            const long hx = (long((node->shapeSize.x * node->scale.x).v) >> 9), hz = (long((node->shapeSize.y * node->scale.y).v) >> 9);
            const long ax = kx_[0] * hx, ay = ky_[0] * hx, bx = kx_[2] * hz, by = ky_[2] * hz;
            const long px4[4] = {-ax - bx, ax - bx, ax + bx, -ax + bx}, py4[4] = {-ay - by, ay - by, ay + by, -ay + by};
            for (int k = 0; k < 4; k++) {
                item.qx[k] = short((cx + px4[k]) >> projShift_);
                item.qy[k] = short((cy + py4[k]) >> projShift_);
            }
            item.layer = 1;
        } else {
            // A particle: a small cube, tumbling. At this size a square of the right size and colour is the cube.
            const long side = (long((node->shapeSize.x * node->scale.x).v) * 28L * gPixelScale) >> 16;
            if (side <= 0) return;
            const int h = int(side / 2), e = int(side - side / 2);
            item.qx[0] = short(sx - h); item.qy[0] = short(sy - h);
            item.qx[1] = short(sx + e); item.qy[1] = short(sy - h);
            item.qx[2] = short(sx + e); item.qy[2] = short(sy + e);
            item.qx[3] = short(sx - h); item.qy[3] = short(sy + e);
            item.layer = 2;
        }
        item.key = makeKey(item.row, item.layer, item.far);
        items_.push_back(item);
    }

    // A flat colour of the game's, as the nearest entry of the ART palette. Looked up once per colour.
    unsigned char artColour(const Vec3 &c)
    {
        const int r = int((long(c.x.v) * 255L) >> 16), g = int((long(c.y.v) * 255L) >> 16), b = int((long(c.z.v) * 255L) >> 16);
        const unsigned long key = ((unsigned long)(r & 255) << 16) | ((unsigned long)(g & 255) << 8) | (unsigned long)(b & 255);
        for (int i = 0; i < colourCount_; i++)
            if (colourKey_[i] == key) return colourIndex_[i];
        long best = 0x7fffffffL;
        int index = 20;
        for (int i = 20; i < 256 && i < sprites_->paletteEntries; i++) { // below 20: transparent, sky, UI and system slots
            const unsigned char *p = sprites_->palette + i * 3;
            const long dr = r - p[0], dg = g - p[1], db = b - p[2], d = dr * dr + dg * dg + db * db;
            if (d < best) {
                best = d;
                index = i;
            }
        }
        if (colourCount_ < 16) {
            colourKey_[colourCount_] = key;
            colourIndex_[colourCount_++] = (unsigned char)index;
        }
        return (unsigned char)index;
    }

    // A convex quad, scanline by scanline. Integers; a few dozen small quads a frame at most.
    static void fillQuad(const BHSurface &s, const short *qx, const short *qy, unsigned char colour)
    {
        int top = qy[0], bottom = qy[0];
        for (int k = 1; k < 4; k++) {
            if (qy[k] < top) top = qy[k];
            if (qy[k] > bottom) bottom = qy[k];
        }
        if (top < 0) top = 0;
        if (bottom >= s.height) bottom = s.height - 1;
        for (int y = top; y <= bottom; y++) {
            int left = 32767, right = -32768;
            for (int k = 0; k < 4; k++) {
                const int x0 = qx[k], y0 = qy[k], x1 = qx[(k + 1) & 3], y1 = qy[(k + 1) & 3];
                if (y0 == y1) {
                    if (y != y0) continue;
                    if (x0 < left) left = x0;
                    if (x1 < left) left = x1;
                    if (x0 > right) right = x0;
                    if (x1 > right) right = x1;
                    continue;
                }
                if ((y < y0 && y < y1) || (y > y0 && y > y1)) continue;
                const int x = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
                if (x < left) left = x;
                if (x > right) right = x;
            }
            if (left < 0) left = 0;
            if (right >= s.width) right = s.width - 1;
            if (right < left) continue;
            bh_fill_rect(&s, left, y, right - left + 1, 1, colour);
        }
    }

    void add(cr::Node *node, const SpriteSet &set, long rx, long ry, long rz, int rotFromMatrix, int row)
    {
        // WHERE FIRST, because most nodes fail this and everything below is then never done.
        const int sx = int((ox_ + kx_[0] * rx + kx_[1] * ry + kx_[2] * rz) >> projShift_);
        const int sy = int((oy_ + ky_[0] * rx + ky_[1] * ry + ky_[2] * rz) >> projShift_);
        // No sprite in the set is larger than this, so a point this far outside cannot put a pixel on screen. The
        // row strips are the exception (798 wide, anchored mid-row) and are let through to the exact test.
        if (set.layer != 1 && (sx < -140 * gPixelScale || sx > viewW_ + 140 * gPixelScale || sy < -40 * gPixelScale ||
                               sy > viewH_ + 160 * gPixelScale)) {
            culled_++;
            if (census_) culledBy_[node->model->name]++;
            return;
        }

        // WHICH WAY IS IT FACING? The first bug the user found by playing: the hero never turned. The rotation
        // lives on the CrossyPlayer GROUP (Player::object) and the model hangs on a CHILD, whose own rotation is
        // permanently zero - so beneath a turned parent the direction is read from the WORLD MATRIX (column-major:
        // e[0], e[2] is the X basis), by dot product against the baked directions. No atan2, no normalize():
        // rsqrt() returns zero on this machine. Everything else - trees, cars, rocks - is turned by its OWN
        // rotation.y, and the sector is one integer division.
        int rot = 0;
        const int count = set.rotCount > 0 ? int(set.rotCount) : 1;
        if (count > 1) {
            if (rotFromMatrix > 0) {
                const int stride = BH_ROT_MAX / count; // 12 baked directions, 4 used -> every third one
                const real bx = node->world.e[0], bz = node->world.e[2];
                real best = real(-1000);
                for (int k = 0; k < count; k++) {
                    const Mat4 &m = rotBasis_[k * stride];
                    const real d = bx * m.e[0] + bz * m.e[2];
                    if (d > best) {
                        best = d;
                        rot = k;
                    }
                }
            } else if (node->rotation.y.v != 0) {
                const long twoPi = 411775L, step = twoPi / count; // 16.16
                long a = node->rotation.y.v % twoPi;
                if (a < 0) a += twoPi;
                rot = int(((a + step / 2) / step) % count);
            }
        }
        short id = -1;
        bool flat = false; // a run-over hero lies ON the road: the car that did it drives over him
        if (set.hero) {
            // The hop's rise and fall is placement; the squash/stretch is what changes the picture.
            // The last two phases are not squashes but WRECKS (apps/sw_bake_amiga.cpp, heroScale): the game flattens
            // a run-over hero to (1.7, 0.05, 1.7) and smears one hit from the side to (1, 1.5, 0.2). Those scales
            // are tweened on the GROUP (Player::scale() is object->scale), not on the model node the hop squashes.
            const int kSquashes = BH_HERO_PHASE_COUNT - 2;
            int phase = int(jsRound((node->scale.y - real(0.8)) * real(20))); // 0.05 per phase
            if (phase < 0) phase = 0;
            if (phase >= kSquashes) phase = kSquashes - 1;
            const cr::Node *group = node->parent;
            if (group && group->scale.y < real(0.6)) {
                phase = kSquashes; // the pancake
                flat = true;
            } else if (group && group->scale.z < real(0.6)) {
                phase = kSquashes + 1; // smeared against the car
            }
            id = set.phase[rot][phase];
            if (id < 0) id = set.phase[0][phase];
        } else {
            id = set.rot[rot];
            if (id < 0) id = set.rot[0];
        }
        if (id < 0) {
            if (census_) noid_[node->model->name]++;
            return;
        }

        // Painter's key: the camera looks down its own -z, so -z of the view-space position is the distance.
        const long far = -(kz_[0] * rx + kz_[1] * ry + kz_[2] * rz);
        // A floor's key is recorded BEFORE the exact cull, so what lies on it can still find it.
        if (set.layer == 1) floorFar_ = far;

        const BHSpriteEntry &entry = sprites_->entries[id];
        const int left = sx - (int)entry.anchorX, top = sy - (int)entry.anchorY;
        const bool cut = left >= viewW_ || top >= viewH_ || left + (int)entry.w <= 0 || top + (int)entry.h <= 0;
        if (detail_) {
            printf("node: %-20s rel=(%ld,%ld)/256 sx=%d sy=%d box=%dx%d at %d,%d far=%ld layer=%d %s\n",
                   node->model->name.c_str(), rx, rz, sx, sy, (int)entry.w, (int)entry.h, left, top, far,
                   (int)set.layer, cut ? "CUT" : "drawn");
        }
        if (cut) {
            culled_++;
            if (census_) culledBy_[node->model->name]++;
            return;
        }

        Item item;
        item.far = int32_t(far);
        item.id = id;
        item.x = short(sx);
        item.y = short(sy);
        item.row = short(row);
        item.colour = 0;
        item.clipY = 0;
        item.layer = set.layer == 1 ? 0 : (set.layer == 2 || flat) ? 1 : 2;
        if (set.hero && heroDead_) {
            // DROWNED. The game sinks the body below the water's surface (WaterRow: getPlayerSunkenPosition) and in
            // 3D the water hides what is under it. A sprite has no water to hide behind, so the whole bird hung
            // there turning slowly - "that is silly", said the user. The surface is a line on screen: nothing of
            // the hero is drawn at or below where the water's top (y = 0.125) lands under him.
            const cr::Node *group = node->parent ? node->parent : node;
            if (group->position.y < real(0.3) && group->scale.y >= real(0.6)) {
                const long wy = (long(real(0.125).v) - long(origin_.y.v)) >> 8;
                const int line = int((oy_ + ky_[0] * rx + ky_[1] * wy + ky_[2] * rz) >> projShift_);
                item.clipY = short(line < 1 ? 1 : line);
            }
        }
        item.key = makeKey(item.row, item.layer, item.far);
        if (item.layer == 0) floorsDrawn_++;
        items_.push_back(item);
        if (census_) drawn_[node->model->name]++;
    }

    // ONE 32-BIT KEY, AND THE SORT MOVES INDICES, NOT ITEMS. The first version compared three fields and shuffled
    // 30-byte structs: 2.6 ms a frame for ninety items. Painted earlier = smaller key:
    //   bits 31..20  the row, farthest first   (rows are counted up the world, so 2047 - row)
    //   bits 19..18  the layer                 (ground, lying on it, standing on it)
    //   bits 17..0   the depth, far first      (the projection's far key, 8 bits dropped, clamped)
    static uint32_t makeKey(int row, int layer, long far)
    {
        long r = 2047L - long(row);
        if (r < 0) r = 0;
        if (r > 4095) r = 4095;
        long d = 131072L - (far >> 8);
        if (d < 0) d = 0;
        if (d > 262143L) d = 262143L;
        return (uint32_t(r) << 20) | (uint32_t(layer & 3) << 18) | uint32_t(d);
    }
    void sortItems(std::vector<Item> &v)
    {
        // Insertion sort (no std::sort - see the note at the top of this file about the toolchain's heap-sort
        // miscompile) over an index array; the items themselves never move.
        const size_t n = v.size();
        order_.resize(n);
        for (size_t i = 0; i < n; i++) order_[i] = (unsigned short)i;
        for (size_t i = 1; i < n; i++) {
            const unsigned short idx = order_[i];
            const uint32_t key = v[idx].key;
            size_t j = i;
            while (j > 0 && v[order_[j - 1]].key > key) {
                order_[j] = order_[j - 1];
                j--;
            }
            order_[j] = idx;
        }
    }
    std::vector<unsigned short> order_;

    const BHSprites *sprites_;
    int diagLeft_ = 10; // one-off placement diagnostic: the first few objects of the first frame
    Mat4 viewRel_, viewProj_;
    Vec3 origin_;
    std::map<const Model *, SpriteSet> sets_;

    // One matrix per baked direction; only e[0] and e[2] are ever read (the X basis, flattened to the ground).
    Mat4 rotBasis_[BH_ROT_MAX];

    // WHERE EVERY NODE WENT, by model name. Compare mode only - these are std::string map lookups per node and
    // have no business in the game loop. It exists because the first comparison against the 3D renderer showed
    // EMPTY ROADS: every vehicle missing, while trees, logs and boulders were fine. A percentage of differing
    // pixels cannot name the class of object that was lost; this can.
    // The ground, drawn before anything standing on it, and the distance to the last floor seen - a lily pad is
    // placed just behind it. Both come straight from the 3D renderer's structure.
    long kx_[3], ky_[3], kz_[3], ox_ = 0, oy_ = 0; // the integer projection - see init()
    static const int kNoRow = -32768;
    long worldZ_ = 0;
    int floorsDrawn_ = 0;
    int needClear_ = 2; // wipe the first frames; after that only when a sentinel says the sky is showing
    const cr::Node *heroNode_ = 0;
    bool heroDead_ = false;
    int heroRow_ = 0;
    unsigned long colourKey_[16];
    unsigned char colourIndex_[16];
    int colourCount_ = 0;
    int32_t floorFar_ = 0;

    bool census_ = false;
    // The per-node lines belong to ONE frame - the dumped one. Printed every frame they are 383 lines times
    // hundreds of frames, which drowns the log and slows the very run being measured.
    bool detail_ = false;
    std::map<std::string, int> seen_, drawn_, culledBy_, unmapped_, noid_;

    static int censusValue(const std::map<std::string, int> &m, const std::string &key)
    {
        std::map<std::string, int>::const_iterator it = m.find(key);
        return it == m.end() ? 0 : it->second;
    }
    std::vector<Item> items_;
};

// The game asks for sounds by name (Game::takeSounds), the container knows them by index. Looked up once per name
// and remembered - including a miss, so a name we do not ship is searched for once and never again.
class SoundBoard {
public:
    void init(BHSounds *sounds) { sounds_ = sounds; }

    void playRequested(Game &game)
    {
        const std::vector<std::string> names = game.takeSounds();
        for (size_t i = 0; i < names.size(); i++) {
            std::map<std::string, int>::iterator it = ids_.find(names[i]);
            if (it == ids_.end()) {
                const int id = bh_sounds_find(sounds_, names[i].c_str());
                it = ids_.insert(std::make_pair(names[i], id)).first;
                if (id < 0) printf("audio: no sound named %s\n", names[i].c_str());
            }
            (void)it;
            play(names[i]);
        }
    }

    unsigned long played() const { return played_; }

    // 0..10, the settings screen's "Sounds"
    int master = 10;

    void play(const std::string &name)
    {
        std::map<std::string, int>::iterator it = ids_.find(name);
        if (it == ids_.end()) {
            const int id = bh_sounds_find(sounds_, name.c_str());
            it = ids_.insert(std::make_pair(name, id)).first;
            if (id < 0) printf("audio: no sound named %s\n", name.c_str());
        }
        if (it->second < 0 || master <= 0) return;
        // the per-sound balance the console builds use (game/sound_volume.h), times the player's setting
        const long v = (long(kSfxVolume) * master / 10 * long(soundVolume(name).v)) >> 16;
        bh_audio_play(sounds_, it->second, int(v > 64 ? 64 : v));
        played_++;
    }

private:
    BHSounds *sounds_ = nullptr;
    std::map<std::string, int> ids_;
    unsigned long played_ = 0;
};

// THE SESSION: game + shared screens + settings + career + music, wired EXACTLY as the SF2000 core wires them
// (src/sf2000/libretro_core.cpp, step()), which is itself apps/bobrhopper.cpp's doStep. The first two Amiga drafts
// had a state machine of my own here - which is why "play again" looped, why the banners appeared without their
// animation or sounds, and why there was no pause, no settings, no career and no ranks. None of that had to be
// written: it had to be CALLED.
struct Session {
    Game *game = nullptr;
    Screens screens;
    UserSettings settings;
    Input input; // cr::Input, never AmigaDOS's Input(): no <proto/*> header reaches this file
    SoundBoard *board = nullptr;
    bool haveSounds = false;
    std::vector<std::string> music; // manifest order: the title song first, then the game tracks
    std::map<std::string, std::string> conf;
    int careerLevel = 1, pendingLevel = 0, musicState = -1, gameTrack = -1, loggedState = -1;
    bool careerDirty = false, settingsDirty = false, selectCombo = false, quit = false;
    bool goArmA = false, goArmMenu = false, pendingClassic = false;

    // ---- the config: key=value lines next to the binary. stdio only (C++ streams never close on this libc), and
    // no rename/fsync games - AmigaDOS writes are not atomic anyway and the file is a dozen lines.
    static const char *confPath() { return "PROGDIR:bobrhopper.cfg"; }
    int getInt(const char *key, int fallback) const
    {
        std::map<std::string, std::string>::const_iterator it = conf.find(key);
        return it == conf.end() ? fallback : atoi(it->second.c_str());
    }
    void setInt(const char *key, int v)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", v);
        conf[key] = buf;
    }
    void loadConf()
    {
        FILE *f = fopen(confPath(), "r");
        if (!f) return;
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = 0;
                char *v = eq + 1;
                size_t n = strlen(v);
                while (n && (v[n - 1] == '\n' || v[n - 1] == '\r' || v[n - 1] == ' ')) v[--n] = 0;
                conf[line] = v;
            } else {
                // the first draft's one-line format: "best 41 character 1" - the record must survive the upgrade
                int best = 0, ch = 0;
                if (sscanf(line, "best %d character %d", &best, &ch) >= 1) {
                    setInt("highscore", best);
                    conf["character"] = ch == 1 ? "chicken" : "beaver";
                }
            }
        }
        fclose(f);
    }
    void saveConf()
    {
        FILE *f = fopen(confPath(), "w");
        if (!f) {
            printf("config: cannot write %s\n", confPath());
            return;
        }
        for (std::map<std::string, std::string>::const_iterator it = conf.begin(); it != conf.end(); ++it)
            fprintf(f, "%s=%s\n", it->first.c_str(), it->second.c_str());
        fclose(f);
    }
    static int clampInt(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

    // Only the beaver and the chicken are baked as sprites; the other six characters of the console builds would
    // be an invisible hero here. The settings entry therefore steps between the two.
    static const int kShippedCharacters = 2;

    void loadSettings()
    {
        loadConf();
        settings.volume = clampInt(getInt("volume", 10), 0, 10);
        settings.shadows = clampInt(getInt("shadows", 0), 0, 2);
        settings.fpsCounter = getInt("fps_counter", 1) != 0; // ON until the port is accepted: the user watches it
        settings.framing = clampInt(getInt("framing", 0), 0, 1);
        settings.language = clampInt(getInt("language", 0), 0, 1);
        settings.music = clampInt(getInt("music_volume", 22), 0, 100);
        const std::string character = conf.count("character") ? conf["character"] : std::string("beaver");
        for (int i = 0; i < kShippedCharacters; i++)
            if (character == kCharacters[i].id) settings.character = i;
        careerLevel = getInt("career_level", 1) < 1 ? 1 : getInt("career_level", 1);
        screens.careerLevel = careerLevel;
        printf("config: volume=%d music=%d language=%d character=%s best=%d career=%d\n", settings.volume,
               settings.music, settings.language, kCharacters[settings.character].id, getInt("highscore", 0),
               careerLevel);
    }
    void saveSettings()
    {
        setInt("volume", settings.volume);
        setInt("shadows", settings.shadows);
        setInt("fps_counter", settings.fpsCounter ? 1 : 0);
        setInt("framing", settings.framing);
        setInt("language", settings.language);
        setInt("music_volume", settings.music);
        conf["character"] = kCharacters[settings.character].id;
        if (game && game->highscore() > getInt("highscore", 0)) setInt("highscore", game->highscore());
        saveConf();
    }
    // Paula's volume is 0..64. 22% is the console default and was tuned there; here it maps onto the 32 the
    // streaming music has played at so far, so the default loudness does not change.
    int musicVolume() const { return clampInt(settings.music * 64 / 44, 0, 64); }
    void applySettings()
    {
        lang::set(settings.language);
        if (board) board->master = settings.volume;
        if (haveSounds) bh_music_volume(musicVolume());
        // Shadows and View are kept as entries so the screen is the same seven lines as on the consoles; the
        // sprites are baked for one framing and carry no cast shadows, so neither changes the picture yet.
    }

    // bobrhopper.cpp's updateMusic: the title song on the home and game over screens, the NEXT game track for
    // every new game - not one track on a loop, which is what the first drafts did.
    void updateMusic()
    {
        if (!haveSounds || music.empty() || int(game->state()) == musicState) return;
        musicState = int(game->state());
        std::string name = music[0];
        if (game->state() == GameState::Playing && music.size() > 1) {
            gameTrack = (gameTrack + 1) % int(music.size() - 1);
            name = music[size_t(gameTrack + 1)];
        }
        const std::string path = "PROGDIR:data/music/" + name + ".wav";
        bh_music_stop();
        if (!bh_music_start(path.c_str(), musicVolume())) printf("music: cannot start %s\n", path.c_str());
        else printf("music: %s\n", name.c_str());
    }

    // one logic step with this step's button mask
    void step(uint16_t mask)
    {
        Game &g = *game;
        input.setSynthetic(mask);
        input.step();
        static const struct {
            Action act;
            Swipe dir;
        } dirs[] = {{ActUp, Swipe::Up}, {ActDown, Swipe::Down}, {ActLeft, Swipe::Left}, {ActRight, Swipe::Right},
                    {ActA, Swipe::Up}};
        if (input.down(ActSelect) && (input.pressed(ActStart) || input.pressed(ActL))) selectCombo = true;
        const bool selectTap = input.released(ActSelect) && !selectCombo;
        if (input.released(ActSelect)) selectCombo = false;

        MenuResult menu;
        const int characterBefore = settings.character;
        const bool menuInput = screens.handleInput(input, settings, menu);
        if (settings.character >= kShippedCharacters) // see kShippedCharacters
            settings.character = settings.character > characterBefore ? 0 : kShippedCharacters - 1;
        if (menu.settingsChanged) {
            applySettings();
            settingsDirty = true;
            if (g.character() != kCharacters[settings.character].id) g.setCharacter(kCharacters[settings.character].id);
        }
        // written once, when the settings screen closes - never per key press (the disk is shared with the music)
        if (settingsDirty && screens.menu() != Menu::Settings) {
            saveSettings();
            settingsDirty = false;
        }
        if (menu.quitToHome) g.quitToHome();
        if (menu.exitGame) quit = true;
        if (menu.startLevel >= 0 && !g.restarting()) {
            if (menu.resetCareer) {
                careerLevel = 1;
                screens.careerLevel = careerLevel;
                setInt("career_level", careerLevel);
                saveConf();
            }
            g.setLevel(menu.startLevel);
            g.startPlaying();
        }
        if (!menuInput) {
            switch (g.state()) {
            case GameState::None:
                if (selectTap) screens.openSettings(false);
                break;
            case GameState::Playing:
                if (input.pressed(ActStart) && !input.down(ActSelect)) {
                    screens.openPause();
                } else {
                    for (int i = 0; i < 5; i++) {
                        if (input.pressed(dirs[i].act)) g.beginMoveWithDirection();
                        if (input.released(dirs[i].act)) g.moveWithDirection(dirs[i].dir);
                    }
                }
                break;
            case GameState::GameOver: {
                // THE TWO BUTTONS UNDER THE BANNERS, as the user asked for this build:
                //   A / FIRE    play again - the next level or the same one in Progression, a new game in Classic
                //   S / LEFT    back to the menu (B too, as on the consoles in Progression)
                // A press only counts if it STARTED on this screen: a player who dies holding left or A (both also
                // hop) would otherwise be thrown to the menu or into a new game by letting go of the key.
                if (input.pressed(ActA)) goArmA = true;
                if (input.pressed(ActSelect) || input.pressed(ActLeft) || input.pressed(ActB)) goArmMenu = true;
                if (goArmA && input.released(ActA)) {
                    if (g.level() > 0) pendingLevel = g.levelDone() ? g.level() + 1 : g.level();
                    else pendingClassic = true;
                    g.restart();
                } else if (goArmMenu && (input.released(ActSelect) || input.released(ActLeft) || input.released(ActB))) {
                    g.restart(); // restart() takes a finished game back to the title screen
                }
                break;
            }
            default:
                break;
            }
        }
        if (g.state() != GameState::GameOver) goArmA = goArmMenu = false;
        if (!screens.pausesGame()) {
            g.step();
            g.endFrame();
        }
        screens.update(g);
        updateMusic();
        if (g.levelDone() && g.level() >= careerLevel) {
            careerLevel = g.level() + 1;
            screens.careerLevel = careerLevel;
            careerDirty = true;
        }
        if (pendingLevel > 0 && !g.restarting() && g.state() == GameState::None) {
            g.setLevel(pendingLevel);
            g.startPlaying();
            pendingLevel = 0;
        }
        if (pendingClassic && !g.restarting() && g.state() == GameState::None) {
            g.setLevel(0);
            g.startPlaying();
            pendingClassic = false;
        }
        if (careerDirty && g.state() != GameState::GameOver) {
            setInt("career_level", careerLevel);
            saveConf();
            careerDirty = false;
        }
        // the record is written once the game-over screen is LEFT, never at the moment of death
        if (g.state() != GameState::GameOver && g.highscore() != getInt("highscore", 0)) {
            setInt("highscore", g.highscore());
            saveConf();
            printf("config: best score %d saved\n", g.highscore());
        }
        if (board) board->playRequested(g);
        if (int(g.state()) != loggedState) {
            loggedState = int(g.state());
            if (g.state() == GameState::GameOver)
                printf(g.levelDone() ? "game: level %d DONE with score %d\n" : "game: hero died on level %d with score %d\n",
                       g.level(), g.score());
            else
                printf("game: state %d, level %d, score %d\n", loggedState, g.level(), g.score());
        }
    }
};

// Raw key codes (RAWKEY) to the game's buttons. The console builds name the buttons A, B, START and SELECT, and
// the shared hint lines say so; on this keyboard they are:
//   cursor keys        move / menu
//   A, Space, Return   A      (choose, hop forward)
//   B, Backspace       B      (back)
//   P, Esc             START  (pause)
//   S, Tab             SELECT (settings, from the title and the game-over screen)
uint16_t buttonForKey(int raw)
{
    switch (raw) {
    case 0x4C: return ActUp;
    case 0x4D: return ActDown;
    case 0x4F: return ActLeft;
    case 0x4E: return ActRight;
    case 0x20: case 0x40: case 0x44: case 0x43: return ActA;
    case 0x35: case 0x41: return ActB;
    case 0x19: case 0x45: return ActStart;
    case 0x21: case 0x42: return ActSelect;
    default: return 0;
    }
}

void dumpFrame(const BHSurface &s, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    for (int y = 0; y < s.height; y++)
        fwrite(s.pixels + (unsigned long)y * (unsigned long)s.pitch, 1, (size_t)s.width, f);
    fclose(f);
    // The size goes NEXT TO the dump, because the drawable area is not a constant: with the Intuition bar
    // visible it is 320x229, not 320x240, and converting the dump at the wrong size simply fails (it did).
    // A sidecar keeps the raw file raw while making it impossible to guess its shape wrong.
    {
        char meta[512];
        int i = 0;
        while (path[i] && i < (int)sizeof(meta) - 6) {
            meta[i] = path[i];
            i++;
        }
        meta[i] = 0;
        // "frame.raw" -> "frame.txt"
        if (i > 4) {
            meta[i - 3] = 't';
            meta[i - 2] = 'x';
            meta[i - 1] = 't';
        }
        FILE *m = fopen(meta, "w");
        if (m) {
            fprintf(m, "%dx%d\n", s.width, s.height);
            fclose(m);
        }
    }
    printf("game: wrote %s (%dx%d)\n", path, s.width, s.height);
}

/* Comparison mode: PROGDIR:compare.txt makes the run deterministic and directly comparable with the 3D
 * renderer. No menu, no autoplay, no input at all - the same seed, the same character, and the frame dumped
 * after a fixed number of LOGIC STEPS rather than frames, because the Amiga draws far more frames than it
 * steps and "frame 400" means a different moment on every machine.
 *
 * The first attempt at this comparison skipped all that and produced a meaningless 78.8% difference: the two
 * pictures were simply different games - different seed, different hops, even a different character. */
struct CompareMode {
    bool on;
    int steps;
    CompareMode() : on(false), steps(600)
    {
        FILE *f = fopen("PROGDIR:compare.txt", "r");
        if (f) {
            int n = 0;
            if (fscanf(f, "%d", &n) == 1 && n > 0) steps = n;
            fclose(f);
            on = true;
            printf("compare: deterministic run, dumping after %d logic steps\n", steps);
        // H2: does the shared RNG produce the SAME sequence on 68k? The row types diverge from the first
        // RANDOMISED row while the hero's position matches to three decimals, so either the generator's
        // arithmetic differs here, or the map draws a different NUMBER of values from it. This settles which.
        // The seed goes through volatile: a probe built from constants measures the compiler, not the machine -
        // a lesson already paid for twice today.
        {
            volatile uint32_t rawSeed = 1;
            Rng probe(rawSeed);
            printf("rng: ");
            for (int i = 0; i < 8; i++) printf("%ld ", (long)probe.next().v);
            printf("(raw 16.16, seed 1)\n");
            // MEASURED: this sequence is bit-identical to mulberry32 computed independently, and identical on
            // the 16.16 and the double path - int(x*4) agrees too, which is the railroad draw. So neither the
            // generator nor realFraction32 explains the missing railroads. What is left is a different NUMBER
            // of draws: some branch in map generation runs differently here and shifts the whole stream. The
            // state after construction says how many were taken.
        }
        }
    }
};

// WHAT THE PLAYER CHOSE IN BobrHopperPrefs (build/amiga/bhprefs.c): PROGDIR:bobrhopper.prefs, "key word" lines.
// These are the settings that decide how the screen is opened, so they are read before it is - which is also why
// they are edited by a separate program and not by a menu drawn on that screen.
//   gfx aga|rtg      screen 320x240 (locked for now)      bar on|off
// rtg.txt, the test rig's old switch, still forces RTG so the existing harness configs keep working.
struct DisplayPrefs {
    int backend = AMIGAGFX_BACKEND_AGA;
    int bar = 1;
    int hires = 0; // 640x480, RTG only
    DisplayPrefs()
    {
        BHPrefs p;
        const int found = bh_prefs_load(&p); // src/amiga/prefs_bh.c - the same parser BobrHopperPrefs uses
        backend = p.rtg ? AMIGAGFX_BACKEND_RTG : AMIGAGFX_BACKEND_AGA;
        bar = p.bar;
        hires = p.hires && p.rtg;
        FILE *f = fopen("PROGDIR:rtg.txt", "r");
        if (f) {
            fclose(f);
            backend = AMIGAGFX_BACKEND_RTG;
        }
        printf("prefs: gfx %s, screen bar %s (%s)\n", backend == AMIGAGFX_BACKEND_RTG ? "rtg" : "aga", bar ? "on" : "off",
               found ? "from bobrhopper.prefs" : "no bobrhopper.prefs - defaults");
    }
};

// An unattended test needs to hop by itself. With PROGDIR:autoplay.txt present the game presses a direction every
// so often, so a run exercises movement, collisions and sounds without anyone at the keyboard - the same idea as
// the sibling ports' autoinput, kept inside the guest where it cannot touch the host's mouse.
struct AutoPlay {
    bool on;
    unsigned long next;
    int scripted;        /* how many scripted key presses are still to come */
    unsigned long nextKey;

    unsigned long hops; /* so the script can turn as well as go forward - see update() */
    /* THE SHARED BOT, the same one apps/sw_game.cpp --scan and the console builds use (src/game/smoke_bot.h).
     *
     * What this replaces was a hop straight up every 45 steps, and that script is the reason two "finished"
     * ports shipped: it never turned (so the hero's facing was never exercised at all) and it walked into the
     * first car it met, so no test ever reached the thirtieth row. The bot only needs GameState and returns a
     * button mask, so the edges - press and release - are computed here the same way Input::step() does it,
     * without dragging in the SDL-shaped Input class.
     */
    cr::SmokeBot bot;
    uint16_t maskPrev;
    bool progression = false, wentDown = false;

    AutoPlay() : on(false), next(0), scripted(0), nextKey(120), hops(0), bot(1u), maskPrev(0)
    {
        FILE *f = fopen("PROGDIR:autoplay.txt", "r");
        if (f) {
            // "prog" in the file picks PROGRESSION from the title menu. Every unattended run before this one
            // started Classic, and the user found by playing that Progression draws nothing but grass.
            char word[8] = {0};
            if (fscanf(f, "%7s", word) == 1 && word[0] == 'p') progression = true;
            fclose(f);
            on = true;
            scripted = 1; // one press of A, to get past the title screen into a game
            printf("game: autoplay is on - hopping by itself (%s)\n", progression ? "PROGRESSION" : "classic");
        }
    }

    /* The shared bot's button mask for this step (it presses A on the title screen by itself). */
    uint16_t mask(Game &game)
    {
        if (!on) return 0;
        // Stand on the game-over screen for four seconds first, so the frame dump shows the banners and the two
        // buttons - the bot's own instant "play again" left nothing to look at.
        if (game.state() == GameState::GameOver) {
            if (overSteps < 240) {
                overSteps++;
                return 0;
            }
        } else {
            overSteps = 0;
        }
        return bot.next(game.state());
    }
    int overSteps = 0;
};

} // namespace

int main(void)
{
    // Unbuffered stdout. The log lines were coming out in the wrong order - a line appearing above the one
    // printed before it - because our buffered stdout and the platform layer's own file writes were racing
    // each other into the same log. With no buffer, what is printed first lands first.
    setvbuf(stdout, 0, _IONBF, 0);
    printf("bobrhopper: start\n");
    {
        // THE HARDWARE DIVIDE AGAINST THE PORTABLE ONE, bit for bit (src/engine/fixed.h). Through volatile, so the
        // compiler cannot fold the test away - a probe made of constants measures the compiler, not the machine.
        static const int32_t kCases[][2] = {{65536, 196608},  {-65536, 196608}, {65536, -196608},    {1, 3},
                                            {12345678, 4321}, {-98765432, 777}, {0x7fffffff, 65536}, {5, 0x7fffffff},
                                            {655360, 3},      {-1, 2},          {32768, 65536},      {1966080, 39322}};
        int bad = 0;
        for (unsigned i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
            volatile int32_t a = kCases[i][0], b = kCases[i][1];
            const int32_t av = a, bv = b;
            const int64_t n = int64_t(av) * 65536;
            const int64_t half = (bv > 0 ? int64_t(bv) : -int64_t(bv)) / 2;
            const int64_t wide = (n >= 0 ? n + half : n - half) / bv;
            const int32_t got = (Fixed::fromRaw(av) / Fixed::fromRaw(bv)).v;
            if (wide >= -2147483647LL && wide <= 2147483647LL && got != int32_t(wide)) {
                bad++;
                printf("divide: %ld / %ld = %ld, expected %ld\n", (long)av, (long)bv, (long)got, (long)wide);
            }
        }
        printf("divide: hardware 64/32 against __divdi3 - %s\n", bad ? "MISMATCH" : "identical on every case");
    }

    Manifest manifest;
    if (!loadManifest(dataDir() + "manifest.txt", manifest)) {
        printf("bobrhopper: no manifest in %s\n", dataDir().c_str());
        return 20;
    }
    ModelLibrary models;
    if (!models.load(manifest, dataDir())) {
        printf("bobrhopper: model metadata failed to load\n");
        return 20;
    }
    printf("bobrhopper: %d models\n", (int)models.models.size());

    // THE DISPLAY SETTINGS COME FIRST: they decide which set of sprites and which font is loaded - only ONE set
    // (the 640 sprites are four times the memory, and a 320 game has no use for them).
    const DisplayPrefs displayPrefs;
    const char *const spritePath = displayPrefs.hires ? "PROGDIR:data/sprites640.spr" : "PROGDIR:data/sprites.spr";
    const char *const fontPath = displayPrefs.hires ? "PROGDIR:data/font640.bhf" : "PROGDIR:data/font.bhf";
    BHSprites sprites;
    if (!bh_sprites_load(&sprites, spritePath)) return 20;

    // The game's own font - the same glyphs, Polish letters included, that every other port of this game
    // draws with. If it will not load the game still plays; the score line simply does not appear, which is
    // better than inventing a second-rate font at runtime.
    BHFont font;
    if (!bh_font_load(&font, fontPath)) printf("bobrhopper: no font - running without the score line\n");

    // Sound is optional on purpose: a machine whose Paula is already taken still plays the game, silently.
    BHSounds sounds;
    const bool haveSounds = bh_sounds_load(&sounds, "PROGDIR:data/sounds.bhs") && bh_audio_open(&sounds);
    SoundBoard board;
    board.init(&sounds);

    // Music is streamed from disk, never loaded: a track is megabytes and chip RAM is two. It rides Paula
    // channels 2 and 3, leaving 0 and 1 for effects, and is refilled once a frame from the main loop.
    if (haveSounds) bh_music_start("PROGDIR:data/music/track_1.wav", 32);

    // The Intuition screen bar the user asked for: the game's name, and the depth gadget that flips back to
    // Workbench. It is Intuition's own bar, never a drawn imitation - and the title had to be set first,
    // because this platform layer came from another project and its bar still said so.
    amigagfx_set_title("Bobr Hopper 68k " BH_VERSION);
    if (displayPrefs.hires) {
        gScreenW = 640;
        gScreenH = 480;
        gPixelScale = 2;
    }
    if (amigagfx_open(gScreenW, gScreenH, displayPrefs.bar, displayPrefs.backend) != 0) {
        printf("bobrhopper: cannot open a screen\n");
        bh_sprites_free(&sprites);
        return 20;
    }
    amigagfx_set_palette(sprites.palette, 0, 256);

    BHSurface surface;
    surface.pixels = amigagfx_chunky();
    surface.pitch = amigagfx_pitch();
    surface.width = amigagfx_game_width();
    surface.height = amigagfx_game_height();

    Game game(models, 1);
    game.context().foam = false; // see GameContext::foam - half the logic step, for squares at the screen's edge
    game.setupGame("beaver");
    game.init();

    AmigaRenderer renderer;
    if (!renderer.init(&sprites, models, surface.width, surface.height)) {
        printf("bobrhopper: no sprites matched the models\n");
        amigagfx_close();
        bh_sprites_free(&sprites);
        return 20;
    }
    // THE SHARED SCREENS, on the Amiga's five overlay calls (src/amiga/shim/engine, src/amiga/ui_amiga.cpp).
    Renderer ui;
    ui.surface = &surface;
    ui.pixelScale = gPixelScale; // the shared screens' 640x480 layout is the screen itself at 640
    ui.sprites = &sprites;
    for (int i = 0; i < BH_SPRITE_NAME_COUNT; i++) {
        const BHSpriteName &n = bh_sprite_names[i];
        if (n.name[0] == 'l' && std::strcmp(n.name, "logo") == 0) ui.logoSprite = n.id;
    }
    ui.font = &font;
    TextRenderer text;
    text.font = &font;
    text.pixelScale = gPixelScale;

    Session session;
    session.game = &game;
    session.board = &board;
    session.haveSounds = haveSounds;
    session.music = manifest.music;
    session.loadSettings();
    session.applySettings();
    // The title picture's SIZE drives the shared layout; its pixels are the baked logo sprite (see the shim).
    if (!session.screens.load(ui, dataDir())) printf("screens: images/*.tex missing - the title has no logo box\n");
    session.screens.settings = &session.settings;
    session.screens.playSound = [&board](const std::string &name) { board.play(name); };
    game.setHighscore(session.getInt("highscore", 0));
    game.setCharacter(kCharacters[session.settings.character].id);
    // GameEngine.unpause() renders - and so ticks the engine - once before the frame loop (bobrhopper.cpp)
    game.tickEngineOnly();
    game.takeSounds();

    // Deterministic comparison run: straight into the game, no menu, no input, dump after a fixed number of
    // LOGIC STEPS. Without this the comparison against the 3D renderer measures nothing, as it did the first
    // time - two different seeds and two different sets of hops are simply two different games.
    CompareMode compare;
    if (compare.on) {
        // Name the row where this machine's random stream parts company with the host build. The map is
        // already different at birth with an identical generator and an identical fx stream, so some branch
        // here draws a different NUMBER of values - and the first row whose state disagrees names it.
        // Count where every node with a model ends up in the dumped frame. The pixel comparison said the roads
        // were EMPTY - the reference draws 770 white pixels below the HUD line, this port drew 5 - and a
        // percentage cannot name the class of object that went missing. This can, and it costs nothing in a
        // normal run because only comparison mode ever switches it on.
        renderer.enableCensus();
        // NO second setupGame/init here. The game above was already built once, and building it again is what
        // made this port look like it generated a different world: GameMap::construct creates maxRows rows of
        // every type and RailRoadRow::construct DRAWS from the map stream, so a second construction ate a few
        // hundred values. Measured as a constant +563 draws before the first row - the entire "the 68k map is
        // different" alarm was this line.
        // The map AT BIRTH, before a single step. The map keeps generating rows as the game runs, so comparing
        // it after 600 steps compares moments, not platforms - which is very likely what made three builds
        // report three different maps while their RNG sequences were bit-identical.
        printf("birth: rng-state map=%lu fx=%lu\n", (unsigned long)game.rng().map.state(),
               (unsigned long)game.rng().fx.state());
        printf("birth: rows");
        for (int rz = 1; rz <= 23; rz++) {
            const RowRef *r = game.map().getRow(real(rz));
            const char *kind = "none";
            if (r) {
                switch (r->type) {
                case RowType::Grass: kind = "grass"; break;
                case RowType::Road: kind = "road"; break;
                case RowType::Water: kind = "water"; break;
                case RowType::RailRoad: kind = "railroad"; break;
                default: kind = "?"; break;
                }
            }
            printf(" %d:%s", rz, kind);
        }
        printf("\n");
        // NOT startPlaying(). The reference side (apps/trace.cpp, apps/sw_game.cpp with no script) leaves the
        // game in state "none" and never takes the first hop, so starting here compared two different states:
        // the trace shows hero (0, 0.4, 8.0) and world (0, 0) on BOTH sides, while a started game scrolls the
        // world away from it. Matching the state is the whole point of this mode.
    }

    // Optional, like every other device here: no joystick means the keyboard alone, not a refusal to start.
    bh_joy_open();
    // What the port really holds, and proof that the decoding obeys the same contract as the keyboard. Neither
    // replaces a person moving a stick - that stays unverified - but together they separate "my code is wrong"
    // from "nothing is plugged into this machine", which one line saying the library opened never could.
    bh_joy_report();
    bh_joy_selftest();

    // WHERE THE LOGIC STEP GOES. Game::step() already times its own stages when given a clock (the SF2000 core's
    // report uses it); the 20 ms DateStamp clock the rest of this file paces itself with cannot see a 7 ms step.
    struct Micros {
        static uint64_t now() { return bh_micros(); }
    };
    AutoPlay autoplay;
    {
        FILE *f = fopen("PROGDIR:profile.txt", "r");
        gProfiling = autoplay.on || compare.on || f != 0;
        if (f) fclose(f);
    }
    if (gProfiling && bh_clock_open()) game.profileClock = &Micros::now;
    // A comparison run must have NO input. autoplay.txt is left behind by the smoke test, and its hops would
    // quietly turn the measurement into a different game. Comparison mode wins over it, loudly.
    if (compare.on && autoplay.on) {
        autoplay.on = false;
        printf("compare: autoplay.txt is present but IGNORED - a comparison run takes no input\n");
    }
    AmigaGfxEvent ev;
    const unsigned long start = amigagfx_millis();
    // `steps` is what the clock says the logic OWES; `ran` is what it actually executed.
    unsigned long steps = 0, ran = 0, frames = 0, lastReport = start;
    int gameOverDump = -1;
    static const unsigned long kMilestones[] = {600UL, 1800UL, 3600UL, 6000UL};
    static const char *const kMilestoneRaw[] = {"PROGDIR:deep0.raw", "PROGDIR:deep1.raw", "PROGDIR:deep2.raw",
                                                "PROGDIR:deep3.raw"};
    static const char *const kMilestonePal[] = {"PROGDIR:deep0.pal", "PROGDIR:deep1.pal", "PROGDIR:deep2.pal",
                                                "PROGDIR:deep3.pal"};
    const int kMilestoneCount = 4;
    int milestoneNext = 0;
    bool trainCaught = false;
    bool windowClosed = false;
    GameState lastState = GameState::None;

    // THE BUTTONS, ONE SNAPSHOT PER CHANGE. At 10-20 frames a second a quick tap goes down AND up between two
    // polls; a single "current mask" would never see it. Every change is queued and the logic consumes one
    // snapshot per step, so a press and its release always land on different steps, in order.
    uint16_t keysHeld = 0, joyHeldMask = 0, held = 0;
    std::vector<uint16_t> queued;
    size_t queuedAt = 0;
    if (autoplay.on && autoplay.progression) {
        // Progression from the title: down, A (the career page), A again (Continue) - then the bot takes over.
        const uint16_t script[] = {0, ActDown, 0, ActA, 0, 0, 0, 0, 0, 0, ActA, 0};
        for (unsigned i = 0; i < sizeof(script) / sizeof(script[0]); i++)
            for (int hold = 0; hold < 30; hold++) queued.push_back(script[i]);
    }

    while (!session.quit && !windowClosed) {
        // THE HOST'S "PLEASE LEAVE" (winuae/harness/bh_go.ps1): the test machine is never restarted - every
        // emulator start steals the user's mouse - so a new run begins by asking this one to end. One file open
        // every 32 frames (about two seconds), on the directory the music streams from, reading only.
        if ((frames & 31) == 0) {
            FILE *q = fopen("PROGDIR:quit.req", "r");
            if (q) {
                fclose(q);
                remove("PROGDIR:quit.req");
                printf("game: the host asked me to leave\n");
                break;
            }
        }
        while (amigagfx_poll(&ev)) {
            if (ev.type == AMIGAGFX_EV_QUIT) {
                windowClosed = true;
            } else if (ev.type == AMIGAGFX_EV_KEY) {
                const int raw = ev.code & 0x7F;
                // ESC ON THE TITLE SCREEN LEAVES THE GAME - the title had no way out at all. Everywhere else Esc
                // is START (pause), and the pause menu has its own Exit.
                if (raw == 0x45 && (ev.code & 0x80) == 0 && game.state() == GameState::None &&
                    session.screens.menu() == Menu::None) {
                    session.quit = true;
                    continue;
                }
                const uint16_t button = buttonForKey(raw);
                if (!button) continue;
                if (ev.code & 0x80) keysHeld = uint16_t(keysHeld & ~button);
                else keysHeld = uint16_t(keysHeld | button);
            }
        }
        {
            // THE JOYSTICK AS A SECOND KEYBOARD: what it holds becomes the same buttons, and the two are ORed.
            //   directions -> directions      red fire -> A
            //   blue (2nd button, CD32 B) -> B in the menus, START (pause) during play - a 2-button stick has
            //                                nothing else to pause with
            //   CD32 PLAY -> START            green -> SELECT (S)          yellow -> B
            const unsigned j = bh_joy_held();
            const bool playing = game.state() == GameState::Playing && session.screens.menu() == Menu::None;
            uint16_t m = 0;
            if (j & BH_JOY_UP) m |= ActUp;
            if (j & BH_JOY_DOWN) m |= ActDown;
            if (j & BH_JOY_LEFT) m |= ActLeft;
            if (j & BH_JOY_RIGHT) m |= ActRight;
            if (j & BH_JOY_FIRE) m |= ActA;
            if (j & BH_JOY_FIRE2) m |= playing ? ActStart : ActB;
            if (j & BH_JOY_PLAY) m |= ActStart;
            if (j & BH_JOY_GREEN) m |= ActSelect;
            if (j & BH_JOY_YELLOW) m |= ActB;
            // A button that changes meaning while held (blue, as play starts or stops) must not leave the old
            // meaning stuck down: the mapped mask is rebuilt from scratch every frame, so it cannot.
            joyHeldMask = m;
        }
        {
            const uint16_t now = uint16_t(keysHeld | joyHeldMask);
            if (now != held) {
                held = now;
                queued.push_back(held);
            }
        }

        // Fixed 60 Hz logic, catching up on elapsed time rather than trusting the frame rate: the original counts
        // movement per tick, so the step must never stretch.
        static unsigned long profLogic = 0, profSound = 0, profRender = 0, profUi = 0, profBlit = 0;
        static unsigned long profFrames0 = 0, profRan0 = 0;
        const unsigned long now = amigagfx_millis();
        // A COMPARISON RUN IS DRIVEN BY LOGIC, NEVER BY THE CLOCK. With the wall clock, `want` was already 86 on
        // the very first frame (loading the sprites, sounds and music takes over a second), the catch-up cap of
        // five steps below ran a handful, and "steps = want" then credited the rest WITHOUT RUNNING THEM. The
        // counter said 600 while the world had advanced about 70 - so the dumped frame was a faithful picture of
        // the wrong moment, the cars sat where they had been seconds earlier, and the pixel comparison read that
        // as "the roads are empty". One step per iteration removes the clock from the measurement entirely.
        const unsigned long want = compare.on ? steps + 1UL : ((now - start) * 60UL) / 1000UL;
        int caught = 0;
        const unsigned long tLogic0 = profMicros();
        while (steps < want && caught < 5) { // never spiral: at most five catch-up steps per frame
            if (compare.on) {
                game.step();
                game.endFrame();
            } else {
                uint16_t mask = held;
                if (queuedAt < queued.size()) mask = queued[queuedAt++];
                session.step(uint16_t(mask | autoplay.mask(game)));
            }
            ran++;
            steps++;
            caught++;
        }
        if (queuedAt >= queued.size()) {
            queued.clear();
            queuedAt = 0;
        }
        if (game.state() != lastState) {
            lastState = game.state();
            if (lastState == GameState::GameOver) gameOverDump = 40; // once the banners have flown in
        }
        // Dropping the debt is right for play (never spiral) and wrong for a measurement, which must not depend
        // on how fast the machine happens to be.
        if (!compare.on && steps < want) steps = want;

        profLogic += profMicros() - tLogic0;
        const unsigned long tSound0 = profMicros();
        if (haveSounds) {
            if (compare.on) board.playRequested(game);
            bh_audio_service();
            bh_music_service(); // hands the decoder whatever buffer Paula has finished with
        }

        // The logic for this frame has already run, so this is where "is this the frame being dumped?" is known.
        const bool milestoneDue = autoplay.on && !compare.on && milestoneNext < kMilestoneCount &&
                                  ran >= kMilestones[milestoneNext];
        const bool dumpingNow = milestoneDue || (compare.on && (int)steps >= compare.steps);
        // In a playtest the census runs EVERY frame, because it is also what answers "is the train actually on
        // screen right now?" - and waiting for that to coincide with a milestone is how two perfectly healthy
        // features got recorded as missing. The per-node lines stay off (detail), so this costs a name lookup
        // per node and nothing else; a real game never switches it on.
        // Only on the frame being dumped. It used to run every frame of an unattended run, and a std::map keyed by
        // model NAME, touched once per node, was a large part of what the profile then blamed on the renderer.
        renderer.setCensus(dumpingNow);
        renderer.setDetail(dumpingNow);
        profSound += profMicros() - tSound0;
        const unsigned long tRender0 = profMicros();
        renderer.render(surface, game);
        profRender += profMicros() - tRender0;
        const unsigned long tUi0 = profMicros();
        if (!compare.on) {
            // the shared layout is 640x480 logical pixels, drawn at half size - so it is told twice our height
            const int uiW = surface.width * 2 / gPixelScale, uiH = surface.height * 2 / gPixelScale;
            // The title's bottom-right corner (the consoles' version label) says how to leave: Esc.
            {
                static int labelLanguage = -1;
                if (labelLanguage != lang::current()) {
                    labelLanguage = lang::current();
                    session.screens.versionLabel = std::string("ESC ") + lang::t(lang::Exit);
                }
            }
            session.screens.drawSceneFade(ui, uiW, uiH);
            drawHud(ui, text, game, uiW, uiH);
            session.screens.draw(ui, text, game, uiW, uiH);
        }
        profUi += profMicros() - tUi0;
        const unsigned long tBlit0 = profMicros();
        amigagfx_blit(0, 0, gScreenW, gScreenH);
        profBlit += profMicros() - tBlit0;
        frames++;
        {
            // FPS on the screen bar, right of the name - the user asked to see the number themselves.
            static unsigned long fpsMark = 0, fpsFrames = 0;
            if (fpsMark == 0) fpsMark = now;
            if (now - fpsMark >= 1000UL && session.settings.fpsCounter) {
                static char bar[64];
                const unsigned long span = now - fpsMark, f10 = ((frames - fpsFrames) * 10000UL) / span;
                snprintf(bar, sizeof(bar), "Bobr Hopper 68k " BH_VERSION "   %lu.%lu FPS", f10 / 10UL, f10 % 10UL);
                amigagfx_show_title(bar);
                fpsMark = now;
                fpsFrames = frames;
            }
        }

        // Dump a frame of ACTUAL GAMEPLAY, not of the title screen. The unattended run leaves the menu at
        // frame 120, so dumping there caught the menu every time - useless for comparing against the 3D
        // renderer, which is what this dump is for.
        if (compare.on) {
            // Counted in LOGIC STEPS, so the moment is the same on any machine however fast it draws.
            if ((int)steps >= compare.steps) {
                dumpFrame(surface, "PROGDIR:frame.raw");
                // And the SCREEN as the machine itself sees it - bar, border colour and all. The chunky buffer
                // above proves what we drew; this proves what Intuition is actually displaying, which is the
                // half the host's black window captures could never answer.
                amigagfx_dump_screen("PROGDIR:screen.raw", "PROGDIR:screen.pal");
                // The world state in numbers, so "the two pictures differ" can be told apart from "the two
                // pictures show different worlds". Printed in raw 16.16 - no float reaches this CPU.
                const Vec3 cam = game.cameraPosition();
                printf("compare: dumped after %lu steps (%lu actually executed), score %d\n", steps, ran,
                       game.score());
                printf("compare: hero=(%ld,%ld,%ld) camera=(%ld,%ld,%ld) 16.16\n", (long)game.hero().position().x.v,
                       (long)game.hero().position().y.v, (long)game.hero().position().z.v, (long)cam.x.v,
                       (long)cam.y.v, (long)cam.z.v);
                // The row types, in the same form apps/trace.cpp prints them. The colour histogram said the two
                // pictures carry the same colours in different amounts, which points at the CONTENT of the rows
                // rather than at the projection - and this is the line that settles it. If these match the
                // reference, the remaining difference is the shadows we deliberately left out; if they do not,
                // the map generator disagrees with the PC and that is a far more serious finding.
                {
                    // How far the map's stream has advanced. Compared against the same number from a CR_FIXED
                    // host build, this pins the divergence to a specific row rather than to "somewhere".
                    printf("compare: rng-state map=%lu fx=%lu\n", (unsigned long)game.rng().map.state(),
                           (unsigned long)game.rng().fx.state());
                    printf("compare: rows");
                    for (int rz = 1; rz <= 23; rz++) {
                        const RowRef *r = game.map().getRow(rz);
                        const char *kind = "none";
                        if (r) {
                            switch (r->type) {
                            case RowType::Grass: kind = "grass"; break;
                            case RowType::Road: kind = "road"; break;
                            case RowType::Water: kind = "water"; break;
                            case RowType::RailRoad: kind = "railroad"; break;
                            default: kind = "?"; break;
                            }
                        }
                        printf(" %d:%s", rz, kind);
                    }
                    printf("\n");
                }
                // Every model that appeared in the scene graph this frame, and what became of it: drawn, cut by
                // the screen-box test, mapped to no sprite at all, or mapped to a sprite set that had no id for
                // its rotation. A missing class of object shows up here as a row with seen > 0 and drawn = 0.
                renderer.printCensus();
                break;
            }
        } else if (gProfiling && (frames == 400 || (frames == 120 && game.state() == GameState::Playing))) {
            dumpFrame(surface, "PROGDIR:frame.raw");
        }
        // The TITLE SCREEN as the player actually sees it - system bar, border and all - taken from screen memory
        // for the same reason the gameplay one is: a host-side capture of this machine comes back black. Frame 60
        // is before the unattended run presses anything (its first key is at 120), so this is the menu at rest.
        if (gProfiling && !compare.on && frames == 60) amigagfx_dump_screen("PROGDIR:title.raw", "PROGDIR:title.pal");
        if (gProfiling && gameOverDump > 0 && --gameOverDump == 0)
            amigagfx_dump_screen("PROGDIR:over.raw", "PROGDIR:over.pal");
        if (milestoneDue) {
            printf("playtest: milestone %d at %lu logic steps, score %d, hero z=%ld (16.16)\n", milestoneNext, ran,
                   game.score(), (long)game.hero().position().z.v);
            amigagfx_dump_screen(kMilestoneRaw[milestoneNext], kMilestonePal[milestoneNext]);
            renderer.printCensus();
            milestoneNext++;
        }
        // THE TRAIN, CAUGHT WHEN IT IS REALLY THERE. A train exists on every railroad row in the pool but only
        // rides across it now and then, so "train_front seen 20, drawn 0" says nothing - the pool is parked at
        // world z = -38.9, hundreds of pixels below the window. This waits for a frame that actually drew one.
        if (autoplay.on && !compare.on && !trainCaught && renderer.drawnCount("train_front") > 0) {
            trainCaught = true;
            printf("playtest: a train is on screen at %lu logic steps - dumping\n", ran);
            amigagfx_dump_screen("PROGDIR:train.raw", "PROGDIR:train.pal");
            renderer.printCensus();
        }
        if (gProfiling && now - lastReport >= 5000UL) {
            const unsigned long secs = (now - start) / 1000UL;
            printf("game: %lu frames in %lu s (%lu fps), %lu steps, %lu sprites, %lu sounds, score %d, state %d\n",
                   frames, secs, secs ? frames / secs : frames, steps, (unsigned long)renderer.drawn(),
                   board.played(), game.score(), (int)game.state());
            // WHERE THE TIME GOES, per stage, since the last report. Added after the user measured 1-2 fps on an
            // honest 040: "21 fps" had been taken with cpu_speed=max + JIT, which measures the host PC.
            printf("profile: logic %lu ms, sound %lu, render %lu, ui %lu, blit+c2p %lu (over %lu frames, %lu steps run)\n",
                   profLogic / 1000UL, profSound / 1000UL, profRender / 1000UL, profUi / 1000UL, profBlit / 1000UL,
                   frames - profFrames0, ran - profRan0);
            renderer.profReport();
            {
                const Game::StepProfile &sp = game.stepProfile;
                const unsigned long n = ran - profRan0 ? ran - profRan0 : 1UL;
                printf("profile/logic: gsap %lu us/step, map %lu, hero %lu, frame-end %lu  (%lu steps; total %lu us/step)\n",
                       (unsigned long)(sp.gsap / n), (unsigned long)(sp.map / n), (unsigned long)(sp.hero / n),
                       (unsigned long)(sp.frame / n), n, (unsigned long)((sp.gsap + sp.map + sp.hero + sp.frame) / n));
                game.stepProfile = Game::StepProfile();
                {
                    // HOW MANY ANIMATIONS IS THAT? The tween engine is most of the logic step, and its cost is per
                    // live animation - so count them, nested timelines and their children separately.
                    struct Count {
                        static void walk(const gsap::Timeline &tl, unsigned long &tweens, unsigned long &timelines,
                                         unsigned long &active)
                        {
                            for (const gsap::Animation *a = tl._first; a; a = a->_next) {
                                if (a->_active) active++;
                                if (a->isTimeline()) {
                                    timelines++;
                                    walk(*static_cast<const gsap::Timeline *>(a), tweens, timelines, active);
                                } else {
                                    tweens++;
                                }
                            }
                        }
                    };
                    unsigned long tweens = 0, timelines = 0, active = 0;
                    Count::walk(game.context().gsap->root, tweens, timelines, active);
                    printf("profile/gsap: %lu tweens in %lu timelines, %lu active\n", tweens, timelines, active);
                }
            }
            profLogic = profSound = profRender = profUi = profBlit = 0;
            profFrames0 = frames;
            profRan0 = ran;
            lastReport = now;
        }
        // The frame cap belongs to the UNATTENDED TEST ONLY. It was left in the playable build and quit the
        // game to Workbench while the user was playing - the worst of the faults in that first draft. A person
        // at the keyboard ends the game, nothing else does.
        // A PLAYTEST ENDS WHEN IT HAS THE EVIDENCE, not after twelve seconds. The old cap stopped the run at
        // 1800 frames - about 745 logic steps once the bot had died a few times - so the deep milestones at
        // 1800, 3600 and 6000 steps were never reached and the frames I needed to look at never existed.
        if (autoplay.on && milestoneNext >= kMilestoneCount) {
            printf("playtest: all %d milestones dumped, stopping\n", kMilestoneCount);
            break;
        }
        if (autoplay.on && frames >= 60000) break; // a backstop, not a schedule: ~8 minutes of emulated play
    }

    const unsigned long elapsed = (amigagfx_millis() - start) / 1000UL;
    // On the way out: the record, and which hero the player last chose.
    session.saveSettings();
    printf("game: finished after %lu frames in %lu s (%lu fps), %lu steps, %lu sounds, score %d\n", frames, elapsed,
           elapsed ? frames / elapsed : frames, steps, board.played(), game.score());
    bh_clock_close();
    amigagfx_close();
    bh_joy_close();
    bh_music_stop();
    if (haveSounds) bh_audio_close();
    bh_sounds_free(&sounds);
    bh_sprites_free(&sprites);
    return 0;
}
