// src/Row/{Grass,Water,Road,RailRoad}.ts, 1:1 including the order of random numbers drawn.
#pragma once

#include <map>
#include <memory>
#include <set>
#include <vector>

#include "game/context.h"

namespace cr {

class Player;

// entities of Water (lily pads, logs) and Road (cars); the train uses the same shape
struct RowEntity {
    Node *mesh = nullptr;
    real top = 0, min = 0, mid = 0;
    int dir = 0;
    int width = 0;
    real collisionBox = 0;
    real speed = 0;
};

// Empty behaves like Random in the original (only Solid is special-cased there); Clear is the port's own: walls at the
// edges and nothing else, for the rows around a Progression finish line (O11.3)
enum class Fill { Empty, Solid, Random, Clear };

struct GrassRow {
    Node *object = nullptr; // the Row Object3D, positioned on z
    Node *floor = nullptr;
    std::vector<Node *> entities;
    std::map<int, int> obstacleMap; // x|0 -> entity index
    std::set<int> requiredClear;
    real top = 0.4;
    bool active = false;

    void construct(GameContext &ctx);
    void generate(GameContext &ctx, Fill type, const std::vector<int> &requiredClearPositions);
    std::vector<int> blockedPositions() const;
    // takes the obstacle at x out of the row (the game's path check, GameMap::newRowKind; not in the original)
    void removeObstacle(GameContext &ctx, int x);

private:
    void addObstacle(GameContext &ctx, int x);
};

struct Foam {
    Node *object = nullptr;
    int direction = 1;
    std::vector<Node *> parts;
    void construct(GameContext &ctx, int dir);
    void run(GameContext &ctx);
    // the game (O7.3): kills the parts' animations and hides them until the next run()
    void stop(GameContext &ctx);

private:
    void runAnimation(GameContext &ctx, Node *n, int i);
};

struct WaterRow {
    Node *object = nullptr;
    Node *floor = nullptr;
    std::vector<std::unique_ptr<RowEntity>> entities;
    real sineCount = 0;
    real sineInc = 3.14159265358979323846 / 50;
    real top = 0.25;
    bool active = false;
    std::vector<int> lilyPadPositions;
    Foam foamLeft, foamRight;
    bool foamRunning = false;

    void construct(GameContext &ctx);
    // the game (GameContext::originalBehaviour, O7.3): the foam animates only while the row is near the hero
    void updateFoam(GameContext &ctx, real heroZ);
    void generate(GameContext &ctx, const std::vector<int> &clearPositions);
    // O23: every player in one call. The logs move once and then each player is checked against them, so two heroes
    // cannot make the river run at double speed. With count == 1 the order of everything is what it always was.
    void update(GameContext &ctx, Player *players, int count);
    RowEntity *getRidableForPosition(const Vec3 &position);
    real getPlayerLowerBouncePositionForEntity(const RowEntity &e) const { return e.top + e.mid; }
    real getPlayerSunkenPosition() const { return rsin(sineCount) * real(0.08) - real(0.2); }

private:
    void generateStatic(GameContext &ctx, const std::vector<int> &clearPositions);
    void generateDynamic(GameContext &ctx);
    void bounce(GameContext &ctx, RowEntity &entity, Player &player);
};

struct RoadRow {
    Node *object = nullptr;
    Node *road = nullptr;
    std::vector<std::unique_ptr<RowEntity>> cars;
    real top = 0.3;
    bool active = false;
    real speedFraction = 0; // the random number carGen drew the speed from

    void construct(GameContext &ctx);
    void isFirstLane(GameContext &ctx, bool isFirst);
    // O8/O15 (the game, difficulty.h): the cars' speed from baskets [first, open) of the four, same random number -
    // `open` grows with the score at the start, `first` grows again past 150 points, taking the slowest lanes away
    void applySpeedBaskets(int first, int open);
    void update(GameContext &ctx, Player *players, int count);

private:
    void carGen(GameContext &ctx);
};

struct RailRoadRow {
    Node *object = nullptr;
    Node *railRoad = nullptr;
    Node *light = nullptr, *activeLightA = nullptr, *activeLightB = nullptr;
    RowEntity train;
    real top = 0.5;
    bool active = false;
    bool lightRinging = false;
    int ringCount = 0;
    int timer = 0;
    bool passSoundPending = false; // the train wrapped; its pass sound plays as it approaches (not in the original)

    void construct(GameContext &ctx);
    void update(GameContext &ctx, Player *players, int count);

private:
    void trainShouldCheckCollision(GameContext &ctx, Player &player);
    void startRingingLight(GameContext &ctx);
    void ringLight(GameContext &ctx);
};

} // namespace cr
