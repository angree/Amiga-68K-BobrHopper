// Everything the ported game objects share: node pool, models, RNG streams, the GSAP engine, timers,
// sound requests, and the engine callbacks the TypeScript classes received in their constructors.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "engine/gsap.h"
#include "game/models.h"
#include "game/rng.h"
#include "game/scene.h"

namespace cr {

struct RowEntity;
class Player;

// what Engine.onCollide(obstacle, type, collision) receives
struct Collision {
    real obstacleSpeed = 0; // obstacle.speed (0 when absent: useParticle's default direction)
    bool hasSpeed = false;
    const char *type = "feathers"; // "feathers" | "water"
    const char *kind = "";         // "car" | "train" | "" (undefined)
    // O23 (two players): WHO was hit. Null means the first player, so every existing caller keeps its meaning.
    Player *who = nullptr;
};

// setTimeout replacement on game time
class Timers {
public:
    int after(real seconds, std::function<void()> fn);
    void cancel(int id);
    void update(real dt);
    void clear() { timers_.clear(); }

private:
    struct Timer {
        int id;
        real left;
        std::function<void()> fn;
    };
    std::vector<Timer> timers_;
    int next_ = 1;
};

struct GameContext {
    NodePool *pool = nullptr;
    const ModelLibrary *models = nullptr;
    GameRng *rng = nullptr;
    gsap::Engine *gsap = nullptr;
    Timers *timers = nullptr;
    std::function<void(const std::string &)> playSound;
    std::function<void(const Collision &)> onCollide;
    // Where the game deliberately differs from the original (user reports); the trace harness sets this so its traces
    // still compare with tools/webref:
    //  - train sounds (O3.3/O3.4): the original plays the pass sound for every active railroad the moment its train
    //    wraps behind the far edge (every ~4.6 s per track, also far off screen) and never the alarm; the game plays
    //    the alarm when the lights start and the pass sound as the train arrives, for tracks near the hero only
    //  - a hop started during a hop (O4.1): the original leaves the first hop's animations running, and their end
    //    clears `moving` in the middle of the new hop, so a water row can attach the hero to a log it is only flying
    //    over, and the hero then drifts with that log on the next row; the game stops them
    //  - logs (O4.2): the original's 2-3 logs 5-8 units apart on a 22-unit loop can leave the screen without a log for
    //    seconds; the game puts 4 evenly spaced logs on every moving water row (at least 2 on screen)
    //  - a way forward (O6.1): the original checks a new row only against the free columns of the grass row before
    //    it, so trees can close every column the hero can walk to, and lily pads can sit only on free columns the hero
    //    cannot reach; the game keeps the columns reachable from the start per row (RowRef::reach), puts a lily pad
    //    on one of them and takes down a tree when a grass row closes all of them
    //  - foam (O7.3): the original animates the foam of all 20 pooled water rows; the game only near the hero
    //  - an easier start (O8, game/difficulty.h): limits of dangerous rows in a row and of railroads per 10 rows, and car
    //    and log speeds from baskets that open as the score grows
    //  - water in a chain of hops (O12): the row drowns a standing hero only. The original's hop animations clear
    //    `moving` in the middle of the next hop, so a river still catches a hero hopping across it; the game stops
    //    them (O4.1), which let fast hops carry the hero over a whole water row without touching it. The game drowns
    //    the hero when a hop starts from a water row with no log or lily pad under it (Game::moveWithDirection)
    bool originalBehaviour = false;
    // The river's foam: six small squares a side, two tweens each, on every water row within 18 rows of the hero.
    // A platform may switch it off. Measured on the Amiga (68040, no JIT): 40-70 live tweens at ~50 us each were
    // more than half of the whole logic step, for decoration at x = +-4.5 - the very edge of a 320-pixel view.
    // It draws from the fx random stream only, which feeds nothing but visuals.
    bool foam = true;
    // O23 (two players): every row must offer at least TWO columns to walk on, not one, so the two players are never
    // forced through the same gap. Set by Game::setPlayerCount; it only ever loosens a row, and with one player it
    // stays false, so the rows a single player gets are drawn from the same random numbers as before.
    bool twoPaths = false;
};

} // namespace cr
