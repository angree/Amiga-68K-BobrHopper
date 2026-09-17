#include "player.h"

#include <algorithm>
#include <cmath>

#include "game/rows.h"
#include "game/settings.h"

namespace cr {

using namespace settings;

static const real JS_PI = 3.141592653589793;

void Player::construct(GameContext &ctx, const std::string &characterId)
{
    object = ctx.pool->alloc();
    setCharacter(ctx, characterId);
    reset();
}

void Player::setCharacter(GameContext &ctx, const std::string &characterId)
{
    if (character == characterId && node) return;
    character = characterId;
    Node *n = ctx.models->getNode(ctx.models->hero, characterId, *ctx.pool);
    if (node) ctx.pool->release(node);

    // expo-three utils.scaleLongestSideToSize(node, 1)
    Vec3 mn, mx;
    worldBounds(n, mn, mx);
    real longest = std::max(mx.x - mn.x, std::max(mx.y - mn.y, mx.z - mn.z));
    real s = longest > 0 ? real(1) / longest : real(1);
    n->scale.set(s, s, s);
    // utils.alignMesh(node, { x: 0.5, z: 0.5, y: 1.0 }): position = -box.min - size + size * axis (box after scaling)
    worldBounds(n, mn, mx);
    Vec3 size = mx - mn;
    n->position.x = -mn.x - size.x + size.x * real(0.5);
    n->position.z = -mn.z - size.z + size.z * real(0.5);
    n->position.y = -mn.y - size.y + size.y * real(1.0);
    node = n;
    object->add(n);
}

void Player::moveOnEntity()
{
    if (!ridingOn) return;
    position().x += ridingOn->speed;
    if (initialPosition) initialPosition->x = position().x;
}

void Player::moveOnCar()
{
    if (!hitBy) return;
    real target = hitBy->mesh->position.x;
    position().x += hitBy->speed;
    if (initialPosition) initialPosition->x = target;
}

void Player::stopAnimations(GameContext &)
{
    for (auto &val : animations_)
        if (val) val->pause();
    animations_.clear();
}

void Player::reset()
{
    position().set(0, groundLevel, real(startingRow));
    scale().set(1, 1, 1);
    rotation().set(0, JS_PI, 0);
    initialPosition.reset();
    targetPosition.reset();
    moving = false;
    hitBy = nullptr;
    ridingOn = nullptr;
    ridingOnOffset = 0;
    isAlive = true;
}

void Player::skipPendingMovement()
{
    if (!moving) return;
    position().set(targetPosition->x, targetPosition->y, targetPosition->z);
    if (targetRotation != 0) rotation().y = normalizeAngle(targetRotation);
}

void Player::finishedMovingAnimation()
{
    moving = false;
    // IDLE_DURING_GAME_PLAY is false
}

void Player::stopIdle(GameContext &)
{
    if (idleAnimation_) idleAnimation_->pause();
    idleAnimation_.reset();
    scale().set(1, 1, 1);
}

void Player::idle(GameContext &ctx)
{
    if (idleAnimation_) return;
    stopIdle(ctx);
    // PlayerIdleAnimation extends TimelineMax({ repeat: -1 })
    gsap::Vars o;
    o.repeat = -1;
    idleAnimation_ = ctx.gsap->timeline(o);
    idleAnimation_->to(&scale(), {{'y', playerIdleScale}}, 0.3, gsap::Vars().withEase(gsap::Power1In()))
        .to(&scale(), {{'y', 1}}, 0.3, gsap::Vars().withEase(gsap::Power1Out()));
}

void Player::commitMovementAnimations(GameContext &ctx, std::function<void()> onComplete)
{
    const real t = baseAnimationTime;
    // PlayerPositionAnimation extends TimelineMax({ onComplete })
    real tx = targetPosition->x, ty = targetPosition->y, tz = targetPosition->z;
    real ix = initialPosition->x, iz = initialPosition->z;
    real dx = tx - ix, dz = tz - iz;
    gsap::Vars po;
    po.onComplete = [this, onComplete]() {
        finishedMovingAnimation();
        onComplete();
    };
    std::shared_ptr<gsap::Timeline> positionAnimation = ctx.gsap->timeline(po);
    positionAnimation->to(&position(), {{'x', ix + dx * real(0.75)}, {'y', ty + real(0.5)}, {'z', iz + dz * real(0.75)}}, t)
        .to(&position(), {{'x', tx}, {'y', ty}, {'z', tz}}, t);

    // PlayerScaleAnimation extends TimelineMax
    std::shared_ptr<gsap::Timeline> scaleAnimation = ctx.gsap->timeline();
    scaleAnimation->to(&scale(), {{'x', 1}, {'y', 1.2}, {'z', 1}}, t)
        .to(&scale(), {{'x', 1.0}, {'y', 0.8}, {'z', 1}}, t)
        .to(&scale(), {{'x', 1}, {'y', 1}, {'z', 1}}, t, gsap::Vars().withEase(gsap::BounceOut()));

    gsap::Vars ro;
    ro.withEase(gsap::Power1InOut());
    ro.onComplete = [this]() { rotation().y = normalizeAngle(rotation().y); };
    std::shared_ptr<gsap::Tween> rotationAnimation = ctx.gsap->to(&rotation(), {{'y', targetRotation}}, t, ro);

    animations_ = {positionAnimation, scaleAnimation, rotationAnimation};
    initialPosition = targetPosition; // the same object from now on
}

void Player::runPosieAnimation(GameContext &ctx)
{
    stopIdle(ctx);
    ctx.gsap->to(&scale(), {{'x', 1.2}, {'y', 0.75}, {'z', 1}}, 0.2);
}

void Player::collideWithCar(GameContext &ctx, RoadRow &road, RowEntity &car)
{
    if (moving && rabs(position().z - jsRound(position().z)) > real(0.1)) getHitByCar(ctx, road, car);
    else getRunOverByCar(ctx, road, car);
}

void Player::getRunOverByCar(GameContext &ctx, RoadRow &road, RowEntity &)
{
    position().y = road.top - real(0.05);
    ctx.gsap->to(&scale(), {{'y', 0.05}, {'x', 1.7}, {'z', 1.7}}, 0.2);
    ctx.gsap->to(&rotation(), {{'y', ctx.rng->fx.next() * JS_PI - JS_PI / 2}}, 0.2);
}

void Player::getHitByCar(GameContext &ctx, RoadRow &road, RowEntity &car)
{
    hitBy = &car;
    bool forward = position().z - jsRound(position().z) > 0;
    position().z = road.object->position.z + (forward ? real(0.52) : real(-0.52));
    ctx.gsap->to(&scale(), {{'y', 1.5}, {'z', 0.2}}, 0.15);
    ctx.gsap->to(&rotation(), {{'z', ctx.rng->fx.next() * JS_PI - JS_PI / 2}}, 0.15);
}

} // namespace cr
