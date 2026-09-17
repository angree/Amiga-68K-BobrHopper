// CrossyGameMap + GameMap from src/CrossyGame.ts, 1:1.
#pragma once

#include <map>
#include <memory>
#include <vector>

#include "game/rows.h"

namespace cr {

enum class RowType { None, Grass, Water, Road, RailRoad };

struct RowRef {
    RowType type = RowType::None;
    GrassRow *grass = nullptr;
    WaterRow *water = nullptr;
    RoadRow *road = nullptr;
    RailRoadRow *railRoad = nullptr;
    // the game's path check (GameContext::originalBehaviour): the columns -4..4 (bit x + 4) the hero can stand on in
    // this row walking forward from the starting row; never 0 in the game's rows from the starting row on
    int reach = 0;
    real top() const; // entity.top
};

class GameMap {
public:
    std::vector<std::unique_ptr<GrassRow>> grasses;
    std::vector<std::unique_ptr<WaterRow>> water;
    std::vector<std::unique_ptr<RoadRow>> roads;
    std::vector<std::unique_ptr<RailRoadRow>> railRoads;
    int grassCount = 0, waterCount = 0, roadCount = 0, railRoadCount = 0;
    int rowCount = 0;
    // O11.3 Progression: the row the finish line lies on (0 = Classic, no finish). Set before init(): that row and the
    // one before it are grass with nothing on them, the rows past it are plain green, and the strip is drawn there.
    int finishRow = 0;

    // constructor: builds the pools and adds them to `world`
    void construct(GameContext &ctx, Node *world);

    void reset();
    void init(GameContext &ctx);
    void newRow(GameContext &ctx);
    void tick(GameContext &ctx, Player &player);

    // getRow(index): floorMap[`${index}`] -- only exact integers ever match
    const RowRef *getRow(real index) const;
    bool treeCollision(const Vec3 &position) const;

private:
    enum class Kind { Auto, Grass, RoadType, Water };
    void newRowKind(GameContext &ctx, Kind kind);
    Fill mapRowToObstacle() const;
    std::vector<int> clearPositionsFromGrass(const GrassRow &g) const;
    // O8 (the game): dangerous rows right before the next row, railroads among the `count` rows before it
    int dangerousRowsBefore() const;
    int railRoadsBefore(int count) const;
    std::map<int, RowRef> floorMap_;
    // O11.3: the black-and-white strip of the finish line, one for the whole map (hidden in Classic)
    Node *finishLine_ = nullptr;
};

} // namespace cr
