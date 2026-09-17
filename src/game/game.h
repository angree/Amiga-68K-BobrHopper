// src/GameEngine.ts + the game-state parts of src/app/index.tsx and CrossyScene/particles from
// src/CrossyGame.ts, 1:1. Runs headless: rendering reads the scene graph, it never drives the logic.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/gsap.h"
#include "game/context.h"
#include "game/game_map.h"
#include "game/player.h"

namespace cr {

enum class Swipe { Up, Down, Left, Right };
enum class GameState { None, Playing, Paused, GameOver };

struct ParticleSystem {
    Node *mesh = nullptr; // group
    std::vector<Node *> parts;
    Vec3 color;        // flat colour (MeshPhongMaterial)
    Vec3 partSize;     // box size
};

class Game {
public:
    // one fixed step of 1/60 s
    static constexpr real kDt = 1.0 / 60.0;

    Game(const ModelLibrary &models, uint32_t seed);

    // GameEngine.setupGame(character) + init(): what the React component does on mount
    void setupGame(const std::string &character);
    void init();

    // input, as GestureView delivers it: key down = beginMoveWithDirection, key up = the swipe
    void beginMoveWithDirection();
    void moveWithDirection(Swipe direction);

    // updateWithGameState(playing) from the home screen: stopIdle + first hop
    void startPlaying();
    // game over -> "none" (long_play button): newScore() + transitionToGamePlayingState()
    void restart();
    // port: leave a running game from the pause menu — ends it like a game over and takes restart()'s way home
    void quitToHome();
    // O11.3 Progression: level 1, 2, ... (0 = Classic, the endless game). Level k ends at a finish line 10 * k rows
    // from the start; setting it builds a new scene, so call it from the home screen, never during play.
    void setLevel(int level);
    int level() const { return level_; }
    int levelRows() const { return level_ * 10; }
    // the finish line was crossed: the level is done and the hero is alive (the state is GameOver either way)
    bool levelDone() const { return levelDone_; }
    // restart()'s fade still has a new scene coming: setLevel() and startPlaying() must wait for it
    bool restarting() const { return restartFramesLeft_ > 0; }
    // index.tsx UNSAFE_componentWillReceiveProps: a new character swaps the hero's model at once; the next
    // setupGame (restart) uses it too
    void setCharacter(const std::string &characterId);

    // browser frame: GSAP ticker, timers, engine tick, deferred work
    void step();
    // React commits setState calls between frames: call after step() once the frame's state was read
    void endFrame() { commitState(); }
    // Engine.tick alone, without the GSAP ticker or timers: GameEngine.unpause() renders (and ticks) once
    // synchronously when the GL context appears, before the animation-frame loop
    void tickEngineOnly();

    // For trace comparisons: when setupGame replaced the hero during this step, the hero as it was just
    // before (the original's trace still holds a reference to that object in that frame).
    struct HeroSnapshot {
        Vec3 position, rotation, scale;
        bool isAlive, moving, riding, hit;
    };
    const HeroSnapshot *heroReplacedThisStep() const { return heroReplaced_ ? &heroBefore_ : nullptr; }

    // state for rendering and UI
    Node *sceneRoot() { return scene_; }
    Node *world() { return world_; }
    Node *worldWithCamera() { return worldWithCamera_; }
    Player &hero() { return hero_; }
    GameMap &map() { return *map_; }
    const ParticleSystem &feathers() const { return feathers_; }
    const ParticleSystem &waterParticles() const { return water_; }
    GameState state() const { return state_; }
    int score() const { return score_; }
    int highscore() const { return highscore_; }
    void setHighscore(int v) { highscore_ = v; }
    // profile of step() in microseconds, summed while profileClock is set (the SF2000 core's game report); a null
    // clock changes nothing
    uint64_t (*profileClock)() = nullptr;
    struct StepProfile {
        int64_t gsap = 0, map = 0, hero = 0, frame = 0;
    };
    StepProfile stepProfile;
    const std::string &character() const { return character_; }
    Vec3 cameraPosition() const { return cameraPosition_; }
    uint64_t steps() const { return steps_; }

    // sounds requested since the last call (AudioManager.playAsync)
    std::vector<std::string> takeSounds();

    GameContext &context() { return ctx_; }
    GameRng &rng() { return rng_; }
    gsap::Engine &gsapEngine() { return gsap_; }

private:
    void onCollide(const Collision &c);
    void gameOver();
    void forwardScene();
    void updateScore();
    void checkIfUserHasFallenOutOfFrame();
    void rumble();
    void useParticle(const char *type, real direction);
    void runFeathers(real direction);
    void runWater();
    bool isGameEnded() const;

    void playMoveSound();
    void playPassiveCarSound();
    void playDeathSound();
    void playCarHitSound();

    const ModelLibrary &models_;
    GameRng rng_;
    NodePool pool_;
    gsap::Engine gsap_;
    Timers timers_;
    GameContext ctx_;

    Node *scene_ = nullptr, *worldWithCamera_ = nullptr, *world_ = nullptr;
    std::unique_ptr<GameMap> map_;
    Player hero_;
    ParticleSystem feathers_, water_;
    Vec3 cameraPosition_{-1, 2.8, -2.9};
    real camCount_ = 0;

    // React component state: setState() only becomes visible to the engine after the frame, when React
    // commits (the original's onSwipe right after updateWithGameState('playing') still sees "none")
    void setState(GameState s);
    void setScore(int s);
    void commitState();
    GameState state_ = GameState::None;
    int score_ = 0, highscore_ = 0;
    int level_ = 0;          // O11.3: 0 = Classic
    bool levelDone_ = false; // the finish line of this level was crossed
    bool pendingState_ = false, pendingScore_ = false;
    GameState nextState_ = GameState::None;
    int nextScore_ = 0;
    std::string character_ = "chicken";
    int audioFileMoveIndex_ = 0;
    std::vector<std::string> sounds_;
    std::vector<std::function<void()>> nextFrame_;  // requestAnimationFrame callbacks
    std::vector<std::function<void()>> microtasks_; // continuations after `await`
    void runMicrotasks();
    uint64_t steps_ = 0;
    int restartFramesLeft_ = 0; // Animated fade of transitionToGamePlayingState
    HeroSnapshot heroBefore_{};
    bool heroReplaced_ = false;
};

} // namespace cr
