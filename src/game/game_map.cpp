#include "game_map.h"

#include <algorithm>
#include <cmath>

#include "game/difficulty.h"
#include "game/player.h"
#include "game/settings.h"

namespace cr {

using namespace settings;

real RowRef::top() const
{
    switch (type) {
    case RowType::Grass: return grass->top;
    case RowType::Water: return water->top;
    case RowType::Road: return road->top;
    case RowType::RailRoad: return railRoad->top;
    default: return 0;
    }
}

void GameMap::construct(GameContext &ctx, Node *world)
{
    for (int i = 0; i < maxRows; i++) {
        grasses.emplace_back(new GrassRow());
        grasses.back()->construct(ctx);
        water.emplace_back(new WaterRow());
        water.back()->construct(ctx);
        roads.emplace_back(new RoadRow());
        roads.back()->construct(ctx);
        railRoads.emplace_back(new RailRoadRow());
        railRoads.back()->construct(ctx);
        world->add(grasses.back()->object);
        world->add(water.back()->object);
        world->add(roads.back()->object);
        world->add(railRoads.back()->object);
    }

    // O11.3: the finish line, in the game's own style - flat squares lying on the grass, black and white in two rows
    finishLine_ = ctx.pool->alloc();
    finishLine_->visible = false;
    for (int i = 0; i < 18; i++) {
        for (int row = 0; row < 2; row++) {
            Node *square = ctx.pool->alloc();
            square->rotation.x = real(3.141592653589793) / real(2);
            square->shape = Shape::Plane;
            square->shapeSize = {1, real(0.5), 1};
            const bool white = (i + row) % 2 == 0;
            square->shapeColor = white ? Vec3{1, 1, 1} : Vec3{real(0.08), real(0.08), real(0.08)};
            square->position.set(real(i - 9) + real(0.5), 0, real(row) * real(0.5) - real(0.25));
            finishLine_->add(square);
        }
    }
    world->add(finishLine_);
}

const RowRef *GameMap::getRow(real index) const
{
    if (rfloor(index) != index) return nullptr;
    auto it = floorMap_.find(int(index));
    return it == floorMap_.end() ? nullptr : &it->second;
}

bool GameMap::treeCollision(const Vec3 &position) const
{
    const RowRef *row = getRow(int(position.z)); // `${position.z | 0}`
    if (row && row->type == RowType::Grass) return row->grass->obstacleMap.count(int(position.x)) != 0;
    return false;
}

void GameMap::tick(GameContext &ctx, Player &player)
{
    if (!ctx.originalBehaviour)
        for (auto &w : water) w->updateFoam(ctx, player.position().z);
    for (auto &r : railRoads) r->update(ctx, player);
    for (auto &r : roads) r->update(ctx, player);
    for (auto &w : water) w->update(ctx, player);
}

std::vector<int> GameMap::clearPositionsFromGrass(const GrassRow &g) const
{
    std::vector<int> blocked = g.blockedPositions();
    std::vector<int> clear;
    for (int x = -4; x <= 4; x++)
        if (std::find(blocked.begin(), blocked.end(), x) == blocked.end()) clear.push_back(x);
    return clear;
}

// RowRef::reach: columns -4..4 as bits x + 4
static const int kAllColumns = 0x1ff;

static int freeColumns(const GrassRow &g)
{
    int free = 0;
    for (int x = -4; x <= 4; x++)
        if (!g.obstacleMap.count(x)) free |= 1 << (x + 4);
    return free;
}

// the free columns the hero reaches from `entry` walking sideways between the obstacles
static int spreadSideways(int entry, int free)
{
    int reach = entry & free, before;
    do {
        before = reach;
        reach |= ((reach << 1) | (reach >> 1)) & free;
    } while (reach != before);
    return reach;
}

// the columns a log carries the hero to from the columns of `mask`: those at or beyond the first of them in direction dir
static int downstream(int mask, int dir)
{
    if (!mask || dir == 0) return mask ? kAllColumns : 0;
    if (dir > 0) {
        int low = 0;
        while (!(mask & (1 << low))) low++;
        return kAllColumns & ~((1 << low) - 1);
    }
    int high = 8;
    while (!(mask & (1 << high))) high--;
    return (1 << (high + 1)) - 1;
}

// the column of `mask` nearest to the middle
static int middleColumn(int mask)
{
    for (int d = 0; d <= 4; d++) {
        if (mask & (1 << (4 - d))) return -d;
        if (mask & (1 << (4 + d))) return d;
    }
    return 0;
}

// standing on it long enough kills: cars, trains, logs drifting off the screen (lily pads never move: water rows with an
// even z are safe)
static bool dangerousRow(const RowRef &row, int z)
{
    return row.type == RowType::Road || row.type == RowType::RailRoad || (row.type == RowType::Water && z % 2 != 0);
}

int GameMap::dangerousRowsBefore() const
{
    int n = 0;
    for (int z = rowCount - 1; z >= 0; z--) {
        const RowRef *row = getRow(real(z));
        if (!row || !dangerousRow(*row, z)) break;
        n++;
    }
    return n;
}

int GameMap::railRoadsBefore(int count) const
{
    int n = 0;
    for (int z = rowCount - count; z < rowCount; z++) {
        const RowRef *row = z >= 0 ? getRow(real(z)) : nullptr;
        if (row && row->type == RowType::RailRoad) n++;
    }
    return n;
}

Fill GameMap::mapRowToObstacle() const
{
    if (rowCount < 5) return Fill::Solid;
    if (rowCount < 10) return Fill::Empty;
    return Fill::Random;
}

void GameMap::newRow(GameContext &ctx) { newRowKind(ctx, Kind::Auto); }

void GameMap::newRowKind(GameContext &ctx, Kind rowKind)
{
    if (grassCount == maxRows) grassCount = 0;
    if (roadCount == maxRows) roadCount = 0;
    if (waterCount == maxRows) waterCount = 0;
    if (railRoadCount == maxRows) railRoadCount = 0;
    if (rowCount < 10) rowKind = Kind::Grass;
    // O11.3 Progression: nothing dangerous from the row before the finish line on - the last row to cross is bare
    // grass, the finish line lies on the row after it, and behind it there is only green with trees
    if (finishRow > 0 && rowCount >= finishRow - 1) rowKind = Kind::Grass;

    if (rowKind == Kind::Auto) {
        static const Kind types[3] = {Kind::Grass, Kind::RoadType, Kind::Water};
        rowKind = types[int(rfloor(ctx.rng->map.next() * 3))];
    }
    // O8 (the game): past the limit of dangerous rows in a row for this score the row becomes grass (same draws)
    const int rowScore = difficulty::scoreOfRow(rowCount);
    if (!ctx.originalBehaviour && (rowKind == Kind::RoadType || (rowKind == Kind::Water && rowCount % 2 != 0)) &&
        dangerousRowsBefore() >= difficulty::dangerousRowsInARow(rowScore))
        rowKind = Kind::Grass;
    // O15 (the game): and past 150 points there is a smallest run of dangerous rows too - a stretch of grass that
    // short becomes a road instead. Water on an even row carries lily pads, so it would be a rest, not a danger.
    if (!ctx.originalBehaviour && rowKind == Kind::Grass &&
        dangerousRowsBefore() < difficulty::dangerousRowsAtLeast(rowScore))
        rowKind = Kind::RoadType;

    const RowRef *previousRow = getRow(rowCount - 1);
    // the path check: where the hero can come from (the rows before the starting row are behind the hero, who
    // starts on column 0 of the starting row)
    int arrival = kAllColumns;
    if (rowCount == startingRow) arrival = 1 << 4;
    else if (rowCount > startingRow && previousRow) arrival = previousRow->reach;
    const bool keepPath = !ctx.originalBehaviour && rowCount >= startingRow;

    switch (rowKind) {
    case Kind::Grass: {
        GrassRow &g = *grasses[size_t(grassCount)];
        g.object->position.z = real(rowCount);
        std::vector<int> required;
        if (previousRow && previousRow->type == RowType::Water) required = previousRow->water->lilyPadPositions;
        const bool atFinish = finishRow > 0 && (rowCount == finishRow - 1 || rowCount == finishRow);
        g.generate(ctx, atFinish ? Fill::Clear : mapRowToObstacle(), required);
        // obstacles on every column the hero can arrive at: the one nearest the middle goes
        if (keepPath && !(arrival & freeColumns(g))) g.removeObstacle(ctx, middleColumn(arrival));
        RowRef ref;
        ref.type = RowType::Grass;
        ref.grass = &g;
        ref.reach = spreadSideways(arrival, freeColumns(g));
        floorMap_[rowCount] = ref;
        grassCount++;
        break;
    }
    case Kind::RoadType: {
        bool rail = (int(ctx.rng->map.next() * 4)) == 0;
        // O8 (the game): no more railroads among this row and the 9 before it than the score allows (a road instead)
        if (rail && !ctx.originalBehaviour && railRoadsBefore(9) >= difficulty::railroadsPerTenRows(rowScore))
            rail = false;
        // O15 (the game): past 170 points there is a smallest number of railroads per ten rows as well
        if (!rail && !ctx.originalBehaviour &&
            railRoadsBefore(9) < difficulty::railroadsAtLeastPerTenRows(rowScore))
            rail = true;
        if (rail) {
            RailRoadRow &rr = *railRoads[size_t(railRoadCount)];
            rr.object->position.z = real(rowCount);
            rr.active = true;
            RowRef ref;
            ref.type = RowType::RailRoad;
            ref.railRoad = &rr;
            ref.reach = arrival ? kAllColumns : 0;
            floorMap_[rowCount] = ref;
            railRoadCount++;
        } else {
            RoadRow &rd = *roads[size_t(roadCount)];
            rd.object->position.z = real(rowCount);
            const RowRef *prev = getRow(rowCount - 1);
            rd.isFirstLane(ctx, !(prev && prev->type == RowType::Road));
            rd.active = true;
            if (!ctx.originalBehaviour)
                rd.applySpeedBaskets(difficulty::carSpeedFirstBasket(rowScore), difficulty::carSpeedBaskets(rowScore));
            RowRef ref;
            ref.type = RowType::Road;
            ref.road = &rd;
            ref.reach = arrival ? kAllColumns : 0;
            floorMap_[rowCount] = ref;
            roadCount++;
        }
        break;
    }
    case Kind::Water: {
        WaterRow &w = *water[size_t(waterCount)];
        w.object->position.z = real(rowCount);
        w.active = true;
        std::vector<int> clear;
        if (keepPath && arrival != kAllColumns) {
            // the columns the hero can reach, not every free one (after grass, and downstream after logs)
            for (int x = -4; x <= 4; x++)
                if (arrival & (1 << (x + 4))) clear.push_back(x);
        } else if (previousRow && previousRow->type == RowType::Grass) {
            clear = clearPositionsFromGrass(*previousRow->grass);
        }
        w.generate(ctx, clear);
        RowRef ref;
        ref.type = RowType::Water;
        ref.water = &w;
        if (w.lilyPadPositions.empty()) {
            // logs: the game counts only the columns from where the hero boards in the logs' direction - hopping
            // sideways against the current to reach a pad is next to impossible (user report after v013)
            const int dir = w.entities.empty() ? 0 : w.entities[0]->dir;
            ref.reach = !arrival ? 0 : ctx.originalBehaviour ? kAllColumns : downstream(arrival, dir);
        } else {
            for (int x : w.lilyPadPositions)
                if (x >= -4 && x <= 4) ref.reach |= 1 << (x + 4);
            ref.reach &= arrival;
        }
        floorMap_[rowCount] = ref;
        waterCount++;
        break;
    }
    default:
        break;
    }
    rowCount++;
}

void GameMap::reset()
{
    grassCount = waterCount = roadCount = railRoadCount = 0;
    rowCount = 0;
    floorMap_.clear();
}

void GameMap::init(GameContext &ctx)
{
    for (int i = 0; i < maxRows; i++) {
        grasses[size_t(i)]->object->position.z = mapOffset;
        water[size_t(i)]->object->position.z = mapOffset;
        water[size_t(i)]->active = false;
        roads[size_t(i)]->object->position.z = mapOffset;
        roads[size_t(i)]->active = false;
        railRoads[size_t(i)]->object->position.z = mapOffset;
        railRoads[size_t(i)]->active = false;
        railRoads[size_t(i)]->passSoundPending = false;
    }

    // O11.3: the finish line sits on its row, just above the grass floor (top 0.4)
    finishLine_->visible = finishRow > 0;
    finishLine_->position.set(0, real(0.41), finishRow > 0 ? real(finishRow) : mapOffset);

    // row 0 is generated but never registered in floorMap
    grasses[size_t(grassCount)]->object->position.z = real(rowCount);
    grasses[size_t(grassCount)]->generate(ctx, mapRowToObstacle(), {});
    grassCount++;
    rowCount++;

    for (int i = 0; i < maxRows + 3; i++) newRow(ctx);
}

} // namespace cr
