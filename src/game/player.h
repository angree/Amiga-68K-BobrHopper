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

    Vec3 &position() { return object->position; }
    Vec3 &rotation() { return object->rotation; }
    Vec3 &scale() { return object->scale; }

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
    void runPosieAnimation(GameContext &ctx);

    void collideWithCar(GameContext &ctx, RoadRow &road, RowEntity &car);
    void getRunOverByCar(GameContext &ctx, RoadRow &road, RowEntity &car);
    void getHitByCar(GameContext &ctx, RoadRow &road, RowEntity &car);

private:
    std::vector<std::shared_ptr<gsap::Animation>> animations_;
    std::shared_ptr<gsap::Timeline> idleAnimation_;
};

} // namespace cr
