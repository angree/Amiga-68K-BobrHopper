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
    // O23: `player` is 0 or 1; with one player it is always 0 and every existing caller is unchanged.
    void beginMoveWithDirection(int player = 0);
    void moveWithDirection(Swipe direction, int player = 0);

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
    // O23: the second player's character. Defaults to the other of beaver/chicken, so a two-player game never shows
    // the same animal twice without the app having to say anything.
    void setCharacter(int player, const std::string &characterId);

    // O23 TWO PLAYERS. 1 or 2; setting it rebuilds the scene (the starting columns and the rows differ), so call it
    // from the home screen exactly like setLevel().
    void setPlayerCount(int count);
    int playerCount() const { return playerCount_; }
    // Classic 2P is a duel: whoever is left behind by more than this many rows falls out of the frame and dies.
    // Progression 2P is co-operative: the leader is pulled back onto the other player's head instead.
    static const int kMaxGap = 7;
    // which player leads (the one with the largest z among those alive; player 0 when nobody is)
    int leader() const;
    // Classic 2P: 0 or 1 for a winner on score, -1 for a draw (and always 0 with one player)
    int winner() const;

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
    Player &hero() { return heroes_[0]; }
    Player &hero(int player) { return heroes_[player >= 0 && player < playerCount_ ? player : 0]; }
    const Player &hero(int player) const { return heroes_[player >= 0 && player < playerCount_ ? player : 0]; }
    GameMap &map() { return *map_; }
    const ParticleSystem &feathers() const { return feathers_; }
    const ParticleSystem &waterParticles() const { return water_; }
    GameState state() const { return state_; }
    int score() const { return score_[0]; }
    int score(int player) const { return score_[player >= 0 && player < 2 ? player : 0]; }
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
    void updateScore(int player);
    void checkIfUserHasFallenOutOfFrame(int player);
    // O23: this player is out of the game - killed by the gap of a duel, or drowned, or run over. The game itself
    // ends only when no player is left; Progression 2P puts a dead player back on the partner's head instead.
    void killPlayer(int player, const char *particle, real direction);
    void endForPlayer(int player);
    void outOfFrame(int player);
    // O23 Progression 2P: the leader is pulled back onto the other player's head instead of leaving it behind
    void pullBack(int front, int back);
    void reviveOnPartner(int player, int partner);
    // O23: the upper player was left standing in mid-air when the lower one hopped away - it comes down onto
    // whatever its own tile turns out to be (grass, a log, or the water that then drowns it)
    void landAfterCarry(int player);
    // O23: two players who ended up on one tile with neither carried - the higher one is put on the other's head
    void stackOnOneTile();
    // O23: the head-standing rules, the gap that decides both modes, and the wait before a co-op revival. All three
    // return at once with one player.
    void updateCarrying();
    void updateGap();
    void updateRespawn();
    // is THIS player out of the game? With one player it is exactly isGameEnded().
    bool playerBlocked(int player) const;
    void rumble();
    void useParticle(const char *type, real direction, int player);
    // O23: which animal player `player` wears (player 1 gets the opposite of player 0 unless the app says)
    const std::string &characterOf(int player) const;
    void runFeathers(real direction);
    void runWater();
    bool isGameEnded() const;

    void playMoveSound(int player);
    void playPassiveCarSound();
    void playDeathSound(int player);
    void playCarHitSound();

    const ModelLibrary &models_;
    GameRng rng_;
    NodePool pool_;
    gsap::Engine gsap_;
    Timers timers_;
    GameContext ctx_;

    Node *scene_ = nullptr, *worldWithCamera_ = nullptr, *world_ = nullptr;
    std::unique_ptr<GameMap> map_;
    Player heroes_[2];
    int playerCount_ = 1;
    ParticleSystem feathers_, water_;
    Vec3 cameraPosition_{-1, 2.8, -2.9};
    real camCount_ = 0;

    // React component state: setState() only becomes visible to the engine after the frame, when React
    // commits (the original's onSwipe right after updateWithGameState('playing') still sees "none")
    void setState(GameState s);
    void setScore(int player, int s);
    void commitState();
    GameState state_ = GameState::None;
    int score_[2] = {0, 0};
    int highscore_ = 0;
    int level_ = 0;          // O11.3: 0 = Classic
    bool levelDone_ = false; // the finish line of this level was crossed
    bool pendingState_ = false;
    bool pendingScore_[2] = {false, false};
    GameState nextState_ = GameState::None;
    int nextScore_[2] = {0, 0};
    std::string character_ = "chicken";
    mutable std::string character2_; // empty = pick the opposite of character_
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
