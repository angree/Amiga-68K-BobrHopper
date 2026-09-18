#include "rows.h"

#include <algorithm>
#include <cmath>

#include "game/difficulty.h"
#include "game/player.h"
#include "game/settings.h"

namespace cr {

using namespace settings;

static const real JS_PI = 3.141592653589793;

// ---------------------------------------------------------------- Grass (src/Row/Grass.ts)

void GrassRow::construct(GameContext &ctx)
{
    object = ctx.pool->alloc();
    floor = ctx.models->getNode(ctx.models->grass, "0", *ctx.pool);
    object->add(floor);
}

void GrassRow::generate(GameContext &ctx, Fill type, const std::vector<int> &requiredClearPositions)
{
    for (Node *e : entities) ctx.pool->release(e);
    entities.clear();
    obstacleMap.clear();
    requiredClear = std::set<int>(requiredClearPositions.begin(), requiredClearPositions.end());

    // treeGen(type): only Fill.solid is special-cased in the original, Fill.empty behaves like random
    int rowCount = 0;
    int count = int(jsRound(ctx.rng->map.next() * 2)) + 1;
    for (int x = -3; x < 12; x++) {
        int _x = x - 4;
        if (type == Fill::Solid) {
            addObstacle(ctx, _x);
            continue;
        }
        if (x >= 9 || x <= -1) { // walls
            addObstacle(ctx, _x);
            continue;
        }
        if (type == Fill::Clear) continue; // O11.3: the rows at a finish line carry nothing to walk around
        if (rowCount < count) {
            if (_x != 0 && ctx.rng->map.next() > real(0.6)) { // short-circuit: nothing drawn at _x == 0
                addObstacle(ctx, _x);
                rowCount++; // counted even when addObstacle skipped a required-clear position
            }
        }
    }
}

void GrassRow::addObstacle(GameContext &ctx, int x)
{
    if (requiredClear.count(x)) return;
    Node *mesh = ctx.rng->map.next() < real(0.4) ? ctx.models->getRandom(ctx.models->boulder, ctx.rng->map, *ctx.pool)
                                                 : ctx.models->getRandom(ctx.models->tree, ctx.rng->map, *ctx.pool);
    obstacleMap[x] = int(entities.size());
    entities.push_back(mesh);
    floor->add(mesh);
    mesh->position.set(real(x), groundLevel, 0);
}

void GrassRow::removeObstacle(GameContext &ctx, int x)
{
    auto it = obstacleMap.find(x);
    if (it == obstacleMap.end()) return;
    const int index = it->second;
    ctx.pool->release(entities[size_t(index)]);
    entities.erase(entities.begin() + index);
    obstacleMap.erase(it);
    for (auto &kv : obstacleMap)
        if (kv.second > index) kv.second--;
}

std::vector<int> GrassRow::blockedPositions() const
{
    std::vector<int> out;
    for (const auto &kv : obstacleMap) out.push_back(kv.first);
    return out;
}

// ---------------------------------------------------------------- Foam (src/Particles/Foam.ts)

void Foam::construct(GameContext &ctx, int dir)
{
    object = ctx.pool->alloc();
    direction = dir;
    for (int i = 0; i < 6; i++) {
        Node *p = ctx.pool->alloc();
        p->rotation.x = JS_PI / 2;
        p->shape = Shape::Plane; // PlaneGeometry(0.6, 0.6), white MeshPhongMaterial, DoubleSide
        p->shapeSize = {0.6, 0.6, 1};
        p->shapeColor = {1, 1, 1};
        parts.push_back(p);
        object->add(p);
    }
}

void Foam::run(GameContext &ctx)
{
    for (int i = 0; i < int(parts.size()); i++) runAnimation(ctx, parts[size_t(i)], i);
}

void Foam::stop(GameContext &ctx)
{
    for (Node *p : parts) {
        ctx.gsap->killTweensOf(&p->scale);
        ctx.gsap->killTweensOf(&p->position);
        p->visible = false;
    }
}

void Foam::runAnimation(GameContext &ctx, Node *n, int i)
{
    GameContext *c = &ctx;
    // setup(n, i)
    real rx = c->rng->fx.next() * real(0.1 - -0.1) + real(-0.1);
    n->position.set(rx, 0, (real(0.6) / real(int(parts.size()))) * i + real(0.2));
    n->visible = true;
    n->scale.set(0.01, 0.01, 0.01);
    n->rotation.y = c->rng->fx.next() * real(0.6) - real(0.3);

    // animate(n, i)
    const real mScale = 1, mDuration = 0.4, lDuration = 1.2, totalDuration = mDuration + lDuration;
    gsap::Vars grow;
    grow.delay = totalDuration * real(0.2) * i;
    grow.withEase(gsap::BounceOut());
    grow.onComplete = [this, c, n, i, lDuration]() {
        const real lScale = 0.01;
        c->gsap->to(&n->scale, {{'x', lScale}, {'y', lScale}, {'z', lScale}}, lDuration,
                    gsap::Vars().withEase(gsap::Power2In()));
        gsap::Vars drift;
        drift.onComplete = [this, c, n, i]() { runAnimation(*c, n, i); };
        real r = c->rng->fx.next() * real(1.0 - 0.2) + real(0.2);
        c->gsap->to(&n->position, {{'x', n->position.x + r * direction}}, lDuration, drift);
    };
    c->gsap->to(&n->scale, {{'x', mScale}, {'y', mScale}, {'z', mScale}}, mDuration, grow);
}

// ---------------------------------------------------------------- Water (src/Row/Water.ts)

void WaterRow::construct(GameContext &ctx)
{
    object = ctx.pool->alloc();
    floor = ctx.models->getNode(ctx.models->river, "0", *ctx.pool);
    object->add(floor);

    // the original animates the foam of all 20 pooled water rows from the start, parked or off screen (~300 of the ~370
    // live animations); the game runs it only near the hero (updateFoam)
    foamLeft.construct(ctx, 1);
    foamLeft.object->position.set(4.5, 0.2, -0.5);
    if (ctx.originalBehaviour) foamLeft.run(ctx);
    else foamLeft.stop(ctx);
    object->add(foamLeft.object);
    foamRight.construct(ctx, -1);
    foamRight.object->position.set(-4.5, 0.2, -0.5);
    if (ctx.originalBehaviour) foamRight.run(ctx);
    else foamRight.stop(ctx);
    object->add(foamRight.object);
    foamRunning = ctx.originalBehaviour;
}

void WaterRow::updateFoam(GameContext &ctx, real heroZ)
{
    // the camera shows up to ~10 rows ahead of the hero and ~4 behind
    const real ahead = object->position.z - heroZ;
    const bool near = ctx.foam && ahead >= real(-5) && ahead <= real(13);
    if (near == foamRunning) return;
    foamRunning = near;
    if (near) {
        foamLeft.run(ctx);
        foamRight.run(ctx);
    } else {
        foamLeft.stop(ctx);
        foamRight.stop(ctx);
    }
}

static bool isStaticRow(int index) { return index % 2 == 0; }

void WaterRow::generate(GameContext &ctx, const std::vector<int> &clearPositions)
{
    for (auto &e : entities) {
        // the original leaves these running on detached meshes; pooled nodes must not inherit them
        ctx.gsap->killTweensOf(&e->mesh->rotation);
        ctx.gsap->killTweensOf(&e->mesh->position);
        ctx.pool->release(e->mesh);
    }
    entities.clear();
    lilyPadPositions.clear();

    if (isStaticRow(int(object->position.z))) generateStatic(ctx, clearPositions);
    else if (!disableDriftwood) generateDynamic(ctx);
}

void WaterRow::generateStatic(GameContext &ctx, const std::vector<int> &clearPositions)
{
    Rng &r = ctx.rng->map;
    int numItems = int(rfloor(r.next() * 2)) + 2;

    std::vector<real> positions;
    real xPos = rfloor(r.next() * 2 - 4);
    for (int i = 0; i < numItems; i++) {
        positions.push_back(xPos);
        xPos += rfloor(r.next() * 2 + 2);
    }

    const auto isClear = [&clearPositions](int x) {
        return std::find(clearPositions.begin(), clearPositions.end(), x) != clearPositions.end();
    };
    if (!clearPositions.empty() && !ctx.twoPaths) {
        bool hasAccessibleLilyPad = false;
        for (real p : positions)
            if (isClear(int(p))) hasAccessibleLilyPad = true;
        if (!hasAccessibleLilyPad) {
            int clearPos = clearPositions[size_t(int(rfloor(r.next() * real(int(clearPositions.size())))))];
            positions[0] = clearPos;
            std::sort(positions.begin(), positions.end());
        }
    } else if (!clearPositions.empty()) {
        // O23 (two players): TWO pads the players can reach, not one, so neither has to wait for the other's square.
        // Pads already on a reachable column are kept; the rest are moved onto reachable columns nothing sits on,
        // starting from a random one of them so a river's crossings are not always at the same end.
        std::vector<int> onClear;
        for (real p : positions)
            if (isClear(int(p)) && std::find(onClear.begin(), onClear.end(), int(p)) == onClear.end())
                onClear.push_back(int(p));
        std::vector<int> spare;
        for (int c : clearPositions)
            if (std::find(onClear.begin(), onClear.end(), c) == onClear.end()) spare.push_back(c);
        if (onClear.size() < 2 && !spare.empty()) {
            const int start = int(rfloor(r.next() * real(int(spare.size()))));
            size_t slot = 0;
            for (size_t k = 0; onClear.size() < 2 && k < spare.size(); k++) {
                const int c = spare[(size_t(start) + k) % spare.size()];
                while (slot < positions.size() && isClear(int(positions[slot]))) slot++;
                if (slot >= positions.size()) break;
                positions[slot++] = real(c);
                onClear.push_back(c);
            }
        }
        std::sort(positions.begin(), positions.end());
    }

    for (real p : positions) lilyPadPositions.push_back(int(p));

    for (real pos : positions) {
        auto entity = std::unique_ptr<RowEntity>(new RowEntity());
        Node *mesh = ctx.models->getRandom(ctx.models->lilyPad, r, *ctx.pool);
        int width = roundedWidthX(mesh);
        entity->mesh = mesh;
        entity->top = 0.2;
        entity->min = 0.01;
        entity->mid = 0.125;
        entity->dir = 0;
        entity->width = width;
        entity->collisionBox = heroWidth / real(2.0) + real(width) / real(2.0) - real(0.1);
        floor->add(mesh);

        mesh->position.set(pos, 0.125, 0);
        entity->speed = 0;

        // TweenMax.to(rotation, Math.random() * 2 + 2, { y: Math.random() * 1.5 + 0.5, yoyo, repeat: -1, Power2.easeInOut })
        real duration = r.next() * 2 + 2;
        real y = r.next() * real(1.5) + real(0.5);
        gsap::Vars o;
        o.yoyo = true;
        o.repeat = -1;
        o.withEase(gsap::Power2InOut());
        ctx.gsap->to(&mesh->rotation, {{'y', y}}, duration, o);
        entities.push_back(std::move(entity));
    }
}

void WaterRow::generateDynamic(GameContext &ctx)
{
    Rng &r = ctx.rng->map;
    const real fraction = r.next();
    // O8/O15 (the game, difficulty.h): 0.02 + 0.05 r in four equal baskets; the faster ones open with the score and
    // past 160 points the slowest ones are taken away, so a river is never a rest any more
    const int score = difficulty::scoreOfRow(int(object->position.z));
    const int open = ctx.originalBehaviour ? 4 : difficulty::logSpeedBaskets(score);
    const int first = ctx.originalBehaviour ? 0 : difficulty::logSpeedFirstBasket(score);
    const real low = real(0.02) + real(0.0125) * real(first);
    const real high = open >= 4 ? real(0.07) : real(0.02) + real(0.0125) * real(open);
    real speed = fraction * (high - low) + low;
    int numItems = int(rfloor(r.next() * 2)) + 2;
    int xDir = 1;
    if (r.next() > real(0.5)) xDir = -1;
    real xPos = real(-6.0) * xDir;
    // the game (GameContext::originalBehaviour): 4 logs, one every 5.5 units of the 22-unit loop (-11..11), from a
    // random phase, so any 11 units of the row hold at least 2 of them
    const bool evenLogs = !ctx.originalBehaviour;
    if (evenLogs) {
        numItems = 4;
        xPos = (r.next() * real(5.5) - real(11)) * xDir;
    }

    for (int x = 0; x < numItems; x++) {
        auto entity = std::unique_ptr<RowEntity>(new RowEntity());
        Node *mesh = ctx.models->getRandom(ctx.models->log, r, *ctx.pool);
        int width = roundedWidthX(mesh);
        entity->mesh = mesh;
        entity->top = 0.3;
        entity->min = -0.3;
        entity->mid = -0.1;
        entity->dir = xDir;
        entity->width = width;
        entity->collisionBox = heroWidth / real(2.0) + real(width) / real(2.0) - real(0.1);
        floor->add(mesh);

        mesh->position.set(xPos, -0.1, 0);
        entity->speed = speed * xDir;
        if (evenLogs) xPos -= real(5.5) * xDir;
        else xPos -= (r.next() * 3 + 5) * xDir;
        entities.push_back(std::move(entity));
    }
}

void WaterRow::bounce(GameContext &ctx, RowEntity &entity, Player &player)
{
    const real timing = 0.2;
    ctx.gsap->to(&entity.mesh->position, {{'y', entity.min}}, timing * real(0.9));
    gsap::Vars b;
    b.delay = timing;
    ctx.gsap->to(&entity.mesh->position, {{'y', entity.mid}}, timing, b);
    ctx.gsap->to(&player.position(), {{'y', entity.top + entity.min}}, timing * real(0.9));
    gsap::Vars d;
    d.delay = timing;
    ctx.gsap->to(&player.position(), {{'y', entity.top + entity.mid}}, timing, d);
}

void WaterRow::update(GameContext &ctx, Player *players, int count)
{
    if (!active) return;
    const real offset = 11;
    for (auto &e : entities) {
        Vec3 &p = e->mesh->position;
        p.x += e->speed;
        // the original snaps a log to the far edge whatever it overshot, so logs at different step phases drift
        // apart over the minutes; the game's evenly spaced logs (GameContext::originalBehaviour) wrap by the loop
        if (ctx.originalBehaviour) {
            if (p.x > offset && e->speed > 0) p.x = -offset;
            else if (p.x < -offset && e->speed < 0) p.x = offset;
        } else {
            if (p.x > offset && e->speed > 0) p.x -= offset * 2;
            else if (p.x < -offset && e->speed < 0) p.x += offset * 2;
        }
    }

    for (int pi = 0; pi < count; pi++) {
        Player &player = players[pi];
        // O23: a player riding on the other one's head is not in the water at all - the carrier is
        if (player.carriedBy) continue;
        if (!player.moving && !player.ridingOn) {
            for (auto &e : entities) {
                // shouldCheckCollision
                if (jsRound(player.position().z) == object->position.z && player.isAlive) {
                    const Vec3 &m = e->mesh->position;
                    if (player.position().x < m.x + e->collisionBox && player.position().x > m.x - e->collisionBox) {
                        player.ridingOn = e.get();
                        player.ridingOnOffset = player.position().x - m.x;
                        bounce(ctx, *e, player);
                    }
                }
            }
            // shouldCheckHazardCollision
            if (jsRound(player.position().z) == object->position.z && !player.moving) {
                if (!player.ridingOn) {
                    if (player.isAlive) {
                        Collision c;
                        c.type = "water";
                        c.who = &player;
                        ctx.onCollide(c);
                    } else {
                        real y = getPlayerSunkenPosition();
                        sineCount += sineInc;
                        player.position().y = y;
                        player.rotation().y += real(0.01);
                        if (!entities.empty()) player.position().x += entities[0]->speed;
                    }
                }
            }
        }
    }
}

RowEntity *WaterRow::getRidableForPosition(const Vec3 &position)
{
    if (jsRound(position.z) != object->position.z) return nullptr;
    for (auto &e : entities) {
        const Vec3 &m = e->mesh->position;
        if (position.x < m.x + e->collisionBox && position.x > m.x - e->collisionBox) return e.get();
    }
    return nullptr;
}

// ---------------------------------------------------------------- Road (src/Row/Road.ts)

void RoadRow::construct(GameContext &ctx)
{
    object = ctx.pool->alloc();
    road = ctx.models->getNode(ctx.models->road, "1", *ctx.pool);
    object->add(road);
    carGen(ctx);
}

void RoadRow::isFirstLane(GameContext &ctx, bool isFirst)
{
    road->model = ctx.models->road.get(isFirst ? "1" : "0");
}

void RoadRow::carGen(GameContext &ctx)
{
    for (auto &c : cars) ctx.pool->release(c->mesh);
    cars.clear();

    Rng &r = ctx.rng->map;
    speedFraction = r.next();
    real speed = speedFraction * real(0.06) + real(0.02);
    int numCars = int(rfloor(r.next() * 2)) + 1;
    int xDir = 1;
    if (r.next() > real(0.5)) xDir = -1;
    real xPos = real(-6.0) * xDir;

    for (int x = 0; x < numCars; x++) {
        auto car = std::unique_ptr<RowEntity>(new RowEntity());
        Node *mesh = ctx.models->getRandom(ctx.models->car, r, *ctx.pool);
        int width = roundedWidthZ(mesh);
        car->mesh = mesh;
        car->dir = xDir;
        car->width = width;
        car->collisionBox = heroWidth / real(2.0) + real(width) / real(2.0) - real(0.1);
        road->add(mesh);

        mesh->position.set(xPos, 0.25, 0);
        car->speed = speed * xDir;
        mesh->rotation.y = JS_PI / 2 * xDir;
        xPos -= (r.next() * 3 + 5) * xDir;
        cars.push_back(std::move(car));
    }
}

void RoadRow::applySpeedBaskets(int first, int open)
{
    // 0.02 + 0.06 r split into four equal baskets: the same r scaled into the baskets that are in use. first = 0 and
    // open = 4 give carGen's own speed, so nothing changes where the score has opened everything and cut nothing.
    const real low = real(0.02) + real(0.015) * real(first);
    const real high = open >= 4 ? real(0.08) : real(0.02) + real(0.015) * real(open);
    const real speed = speedFraction * (high - low) + low;
    for (auto &c : cars) c->speed = speed * c->dir;
}

void RoadRow::update(GameContext &ctx, Player *players, int count)
{
    if (!active) return;
    const real offset = 11;
    for (auto &c : cars) {
        Vec3 &p = c->mesh->position;
        p.x += c->speed;
        // O23: the car moves and wraps ONCE; only the collision test is per player. A wrapped car is not tested at
        // all this step - that is the original's own `else if` chain, kept exactly, so one player plays as before.
        if (p.x > offset && c->speed > 0) {
            p.x = -offset;
            for (int pi = 0; pi < count; pi++)
                if (c.get() == players[pi].hitBy) players[pi].hitBy = nullptr;
        } else if (p.x < -offset && c->speed < 0) {
            p.x = offset;
            for (int pi = 0; pi < count; pi++)
                if (c.get() == players[pi].hitBy) players[pi].hitBy = nullptr;
        } else {
            for (int pi = 0; pi < count; pi++) {
                Player &player = players[pi];
                if (player.carriedBy) continue; // carried: the one below takes the hit
                if (jsRound(player.position().z) == object->position.z && player.isAlive) {
                    if (player.position().x < p.x + c->collisionBox && player.position().x > p.x - c->collisionBox) {
                        player.collideWithCar(ctx, *this, *c);
                        Collision col;
                        col.obstacleSpeed = c->speed;
                        col.hasSpeed = true;
                        col.type = "feathers";
                        col.kind = "car";
                        col.who = &player;
                        ctx.onCollide(col);
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------- RailRoad (src/Row/RailRoad.ts)

void RailRoadRow::construct(GameContext &ctx)
{
    object = ctx.pool->alloc();
    const ModelLibrary &m = *ctx.models;
    railRoad = m.getNode(m.railroad, "0", *ctx.pool);
    light = m.getNode(m.trainLight, "0", *ctx.pool);
    activeLightA = m.getNode(m.trainLight, "active_0", *ctx.pool);
    activeLightB = m.getNode(m.trainLight, "active_1", *ctx.pool);

    Node *trainMesh = m.trainWithSize(ctx.rng->map.next() * 2 + 1, *ctx.pool);
    int width = roundedWidthX(trainMesh);
    train.mesh = trainMesh;
    train.speed = 0.8;
    train.width = width;
    train.collisionBox = heroWidth / real(2.0) + real(width) / real(2.0) - real(0.1);

    for (Node *l : {light, activeLightA, activeLightB}) {
        l->position.z = -0.5;
        l->rotation.y = JS_PI;
        railRoad->add(l);
    }
    activeLightA->visible = false;
    activeLightB->visible = false;

    railRoad->add(trainMesh);
    trainMesh->position.y = groundLevel;
    trainMesh->position.z = 0.1;
    object->add(railRoad);
}

void RailRoadRow::update(GameContext &ctx, Player *players, int count)
{
    if (!active) return;
    const bool moving = players[0].moving;
    const real offset = 22 * 5;
    Vec3 &p = train.mesh->position;
    p.x += train.speed;
    // the game's train sounds (GameContext::originalBehaviour): only tracks from 3 rows behind the hero to 10 ahead
    // are heard (the camera shows 8-10 ahead and 1-4 behind)
    // O23: with two players the track is heard when it is near EITHER of them - the camera holds both in frame, so
    // a bell that only followed player one would ring for a track off screen and stay silent for one in view.
    real rowsAhead = object->position.z - players[0].position().z;
    for (int pi = 1; pi < count; pi++) {
        const real other = object->position.z - players[pi].position().z;
        if (rabs(other) < rabs(rowsAhead)) rowsAhead = other;
    }
    const bool heard = rowsAhead >= real(-3) && rowsAhead <= real(10);
    if (p.x > offset && train.speed > 0) {
        p.x = -offset;
        startRingingLight(ctx);
        if (ctx.originalBehaviour) {
            ctx.playSound("train_move_0");
        } else {
            if (heard) ctx.playSound("train_alarm");
            passSoundPending = true;
        }
    } else if (p.x < -offset && train.speed < 0) {
        p.x = offset;
        startRingingLight(ctx);
        if (ctx.originalBehaviour) {
            ctx.playSound("train_move_0");
        } else {
            if (heard) ctx.playSound("train_alarm");
            passSoundPending = true;
        }
    } else if (!moving || !ctx.originalBehaviour) {
        // the game also checks a hero in the air over the track, like the cars do: the original checks a standing hero
        // only, so a hop through a passing train killed or not depending on the step it landed on (user report)
        for (int pi = 0; pi < count; pi++)
            if (!players[pi].carriedBy) trainShouldCheckCollision(ctx, players[pi]);
    }
    // The pass sound (1.5 s, 90 steps = 72 units) starts 45 steps before the train's centre reaches x = 0, so its
    // middle is the moment it crosses the hero's column; the 1.54 s alarm from the wrap (137 steps earlier) ends
    // just before it.
    const real kPassSoundX = 36;
    if (passSoundPending && ((train.speed > 0 && p.x >= -kPassSoundX) || (train.speed < 0 && p.x <= kPassSoundX))) {
        passSoundPending = false;
        if (heard) ctx.playSound("train_move_0");
    }
}

void RailRoadRow::trainShouldCheckCollision(GameContext &ctx, Player &player)
{
    if (!(jsRound(player.position().z) == object->position.z && player.isAlive)) return;
    const Vec3 &m = train.mesh->position;
    if (!(player.position().x < m.x + train.collisionBox && player.position().x > m.x - train.collisionBox)) return;

    Vec3 &pp = player.position();
    if (player.moving && rabs(pp.z - jsRound(pp.z)) > real(0.1)) {
        // unreachable in the original (only called when the player is not moving), kept 1:1
        bool forward = pp.z - jsRound(pp.z) > 0;
        pp.z = object->position.z + (forward ? real(0.52) : real(-0.52));
        ctx.gsap->to(&player.scale(), {{'y', 1.5}, {'z', 0.2}}, 0.3);
        ctx.gsap->to(&player.rotation(), {{'z', ctx.rng->fx.next() * JS_PI - JS_PI / 2}}, 0.3);
        Collision c;
        c.obstacleSpeed = train.speed;
        c.hasSpeed = true;
        c.kind = "train";
        c.who = &player;
        ctx.onCollide(c);
        return;
    }
    pp.y = groundLevel;
#ifdef CR_AMIGA
    // The train TAKES the hero with it, as a car does when it hits him from the side (Player::moveOnCar) - the user
    // asked for it while testing the Amiga build: a body left lying between the rails while the train runs through
    // it looks like a missed collision. Amiga only for now: it changes what a death looks like, and the console
    // builds' recorded smoke digests would have to be re-taken before it becomes common behaviour.
    player.hitBy = &train;
#endif
    ctx.gsap->to(&player.scale(), {{'y', 0.2}, {'x', 1.5}}, 0.3);
    ctx.gsap->to(&player.rotation(), {{'y', ctx.rng->fx.next() * JS_PI - JS_PI / 2}}, 0.3);
    {
        Collision c; // this.onCollide() with no arguments
        c.who = &player;
        ctx.onCollide(c);
    }
}

void RailRoadRow::startRingingLight(GameContext &ctx)
{
    lightRinging = true;
    ringCount = 0;
    ringLight(ctx);
}

void RailRoadRow::ringLight(GameContext &ctx)
{
    ctx.timers->cancel(timer);
    if (lightRinging && ringCount < 15) {
        light->visible = false;
        ringCount += 1;
        activeLightB->visible = activeLightA->visible;
        activeLightA->visible = !activeLightA->visible;
        GameContext *c = &ctx;
        timer = ctx.timers->after(0.2, [this, c]() { ringLight(*c); });
    } else {
        lightRinging = false;
        ringCount = 0;
        light->visible = true;
        activeLightB->visible = activeLightA->visible = false;
    }
}

} // namespace cr
