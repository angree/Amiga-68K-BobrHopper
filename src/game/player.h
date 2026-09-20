// src/CrossyPlayer.ts, 1:1.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "game/context.h"

namespace cr {

struct RowEntity;
struct RoadRow;

class Player {
public:
    Node *object = nullptr; // the CrossyPlayer Group
    Node *node = nullptr;   // the character model inside it
    std::string character;

    // JS objects shared by reference: after a hop, initialPosition and targetPosition are the same object
    std::shared_ptr<Vec3> initialPosition, targetPosition;
    real targetRotation = 0; // a JS number; undefined and 0 behave the same in the original (falsy)
    bool moving = false;
    bool isAlive = true;
    RowEntity *hitBy = nullptr;
    RowEntity *ridingOn = nullptr;
    real ridingOnOffset = 0;

    // O23 (two players): which player this is, 0 or 1. Everything the rows and the collisions report carries it, so
    // one hero can drown while the other rides on. With one player it is always 0 and nothing below is ever used.
    int index = 0;
    // standing on the other player's head: whoever is below carries whoever is above. The carried player copies the
    // carrier's x and z every step, so it rides along on a log and falls behind the moment the carrier hops away.
    Player *carriedBy = nullptr;
    Player *carrying = nullptr;
    // O24: this player is in the middle of being MOVED onto the other's head by a rule rather than by its own hop
    // (the co-op pull-back, or two landing on one tile). While it counts down the little arc owns the position and
    // updateCarrying leaves it alone - without this the player simply appeared up there, which is what the author
    // and their brother both reported: "postac gracza sie pojawia na glowie bez animacji".
    int carryHopSteps = 0;
    // Classic 2P: steps this player has been at the edge of the frame, blinking before it falls behind for good
    int warnSteps = 0;

    Vec3 &position() { return object->position; }
    Vec3 &rotation() { return object->rotation; }
    Vec3 &scale() { return object->scale; }
    // O23: the renderers and Game::leader() look at players they must not move
    const Vec3 &position() const { return object->position; }
    const Vec3 &rotation() const { return object->rotation; }
    const Vec3 &scale() const { return object->scale; }

    void construct(GameContext &ctx, const std::string &characterId);
    void setCharacter(GameContext &ctx, const std::string &characterId);

    void moveOnEntity();
    void moveOnCar();
    void stopAnimations(GameContext &ctx);
    void reset();
    void skipPendingMovement();
    void finishedMovingAnimation();
    void stopIdle(GameContext &ctx);
    void idle(GameContext &ctx);
    void commitMovementAnimations(GameContext &ctx, std::function<void()> onComplete);
    // O24: the animations of a move this player did not make itself (Game::hopOntoHead), so the next hop stops them
    // the same way it stops its own
    void setAnimations(std::vector<std::shared_ptr<gsap::Animation>> a) { animations_ = a; }
    void runPosieAnimation(GameContext &ctx);

    void collideWithCar(GameContext &ctx, RoadRow &road, RowEntity &car);
    void getRunOverByCar(GameContext &ctx, RoadRow &road, RowEntity &car);
    void getHitByCar(GameContext &ctx, RoadRow &road, RowEntity &car);

private:
    std::vector<std::shared_ptr<gsap::Animation>> animations_;
    std::shared_ptr<gsap::Timeline> idleAnimation_;
};

} // namespace cr
