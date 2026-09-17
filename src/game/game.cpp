#include "game.h"

#include <algorithm>
#include <cmath>

#include "engine/strings.h"
#include "game/settings.h"

namespace cr {

using namespace settings;

static const real JS_PI = 3.141592653589793;

Game::Game(const ModelLibrary &models, uint32_t seed) : models_(models)
{
    rng_.seed(seed);
    ctx_.pool = &pool_;
    ctx_.models = &models_;
    ctx_.rng = &rng_;
    ctx_.gsap = &gsap_;
    ctx_.timers = &timers_;
    ctx_.playSound = [this](const std::string &s) { sounds_.push_back(s); };
    ctx_.onCollide = [this](const Collision &c) { onCollide(c); };
}

std::vector<std::string> Game::takeSounds()
{
    std::vector<std::string> out;
    out.swap(sounds_);
    // tracks generated together share their train's phase; the game plays each train sound once per step instead of
    // stacking identical copies (louder and clipped); the original's traces keep every request (context.h)
    if (!ctx_.originalBehaviour) {
        std::vector<std::string> merged;
        for (const std::string &s : out)
            if (s.compare(0, 6, "train_") != 0 || std::find(merged.begin(), merged.end(), s) == merged.end())
                merged.push_back(s);
        out.swap(merged);
    }
    return out;
}

// ------------------------------------------------------------------ setup

void Game::setupGame(const std::string &character)
{
    // a new CrossyScene: everything from the previous game goes away (the original keeps the old scene's
    // tweens running on detached objects; the GSAP ticker time continues either way)
    gsap_.clear();
    timers_.clear();
    nextFrame_.clear();
    microtasks_.clear();
    if (scene_) pool_.release(scene_);
    map_.reset(new GameMap());
    hero_ = Player();
    character_ = character;

    scene_ = pool_.alloc();
    worldWithCamera_ = pool_.alloc();
    world_ = pool_.alloc();
    worldWithCamera_->add(world_);
    scene_->add(worldWithCamera_);

    cameraPosition_ = {-1, 2.8, -2.9};
    worldWithCamera_->position.z = real(-startingRow);

    map_->construct(ctx_, world_);
    camCount_ = 0;
    hero_.construct(ctx_, character);
    world_->add(hero_.object);

    // scene.createParticles()
    water_ = ParticleSystem();
    water_.mesh = pool_.alloc();
    water_.color = {real(0x71 / 255.0), real(0xd7 / 255.0), real(0xff / 255.0)};
    water_.partSize = {0.2, 0.3, 0.2};
    for (int i = 0; i < 15; i++) {
        Node *p = pool_.alloc();
        p->shape = Shape::Box;
        p->shapeSize = water_.partSize;
        p->shapeColor = water_.color;
        water_.parts.push_back(p);
        water_.mesh->add(p);
    }
    world_->add(water_.mesh);
    feathers_ = ParticleSystem();
    feathers_.mesh = pool_.alloc();
    feathers_.color = {1, 1, 1};
    feathers_.partSize = {0.1, 0.1, 0.01};
    for (int i = 0; i < 20; i++) {
        Node *p = pool_.alloc();
        p->shape = Shape::Box;
        p->shapeSize = feathers_.partSize;
        p->shapeColor = feathers_.color;
        feathers_.parts.push_back(p);
        feathers_.mesh->add(p);
    }
    world_->add(feathers_.mesh);
}

void Game::setState(GameState s)
{
    pendingState_ = true;
    nextState_ = s;
}

void Game::setScore(int s)
{
    pendingScore_ = true;
    nextScore_ = s;
}

void Game::commitState()
{
    if (pendingScore_) score_ = nextScore_;
    if (pendingState_) {
        state_ = nextState_;
        // ScoreText: useEffect on gameOver -> setHighscore(score) when it is a new best (the best score belongs to
        // Classic: a Progression level ends at its finish line, so its score says nothing about how far one gets)
        if (state_ == GameState::GameOver && level_ == 0 && score_ > highscore_) highscore_ = score_;
    }
    pendingState_ = pendingScore_ = false;
}

void Game::init()
{
    setScore(0); // onGameInit
    cameraPosition_.z = 1;
    hero_.reset();
    // O11.3: the finish line of this level, at the row the score `levelRows()` is reached on
    levelDone_ = false;
    map_->finishRow = level_ > 0 ? startingRow + levelRows() : 0;

    // scene.resetParticles(hero.position)
    feathers_.mesh->position = hero_.position();
    water_.mesh->position = hero_.position();
    feathers_.mesh->position.y = 0;
    water_.mesh->position.y = 0;

    camCount_ = 0;
    map_->reset();
    hero_.idle(ctx_);
    map_->init(ctx_);
}

void Game::startPlaying()
{
    // updateWithGameState(playing)
    if (state_ == GameState::Playing) return;
    GameState last = state_;
    setState(GameState::Playing);
    if (last == GameState::None) {
        hero_.stopIdle(ctx_);
        // onSwipe(SWIPE_UP): ignored, React has not committed "playing" yet, so the first press never hops
        moveWithDirection(Swipe::Up);
    }
}

void Game::restart()
{
    // updateWithGameState(none) after gameOver (verified against the original with tools/webref/trace.mjs):
    //  - transitionToGamePlayingState() starts a 200 ms Animated fade; its completion callback, 12 frames
    //    later and after that frame's engine tick, runs setupGame(character) + init() on a new scene
    //  - newScore(): setState({score: 0}) and engine.init() right away, on the OLD scene (the world is not
    //    moved back, so the camera glides home and forwardScene may add a row meanwhile)
    if (state_ != GameState::GameOver) return;
    setState(GameState::None);
    restartFramesLeft_ = 12;
    setScore(0);
    init();
}

void Game::quitToHome()
{
    if (state_ != GameState::Playing) return;
    hero_.moving = false;
    hero_.stopAnimations(ctx_);
    state_ = GameState::GameOver; // committed at once: nothing observes the intermediate state
    pendingState_ = false;
    restart();
}

void Game::setLevel(int level)
{
    if (level == level_) return;
    level_ = level;
    // a level's map differs from the first row on (the finish line and the rows around it), so the scene is rebuilt
    // the way the restart fade does it
    setupGame(character_);
    init();
}

void Game::setCharacter(const std::string &characterId)
{
    character_ = characterId;
    hero_.setCharacter(ctx_, characterId);
}

// ------------------------------------------------------------------ frame

void Game::runMicrotasks()
{
    while (!microtasks_.empty()) {
        std::vector<std::function<void()>> tasks;
        tasks.swap(microtasks_);
        for (auto &fn : tasks) fn();
    }
}

void Game::step()
{
    // One browser frame. Animation-frame callbacks run in registration order, each followed by a
    // microtask checkpoint: GSAP ticker, the engine's render loop, callbacks queued during the previous
    // frame (particles), the Animated fade. Timers (train light) fire after the frame.
    steps_++;
    heroReplaced_ = false;
    const uint64_t p0 = profileClock ? profileClock() : 0;
    gsap_.tick();
    const uint64_t p1 = profileClock ? profileClock() : 0;

    std::vector<std::function<void()>> raf;
    raf.swap(nextFrame_);

    // Engine.tick
    map_->tick(ctx_, hero_);
    const uint64_t p2 = profileClock ? profileClock() : 0;
    if (!hero_.moving) {
        hero_.moveOnEntity();
        hero_.moveOnCar();
        checkIfUserHasFallenOutOfFrame();
    }
    forwardScene();
    runMicrotasks();
    const uint64_t p3 = profileClock ? profileClock() : 0;
    if (profileClock) {
        stepProfile.gsap += int64_t(p1 - p0);
        stepProfile.map += int64_t(p2 - p1);
        stepProfile.hero += int64_t(p3 - p2);
    }

    for (auto &fn : raf) {
        fn();
        runMicrotasks();
    }

    if (restartFramesLeft_ > 0 && --restartFramesLeft_ == 0) {
        heroBefore_ = {hero_.position(), hero_.rotation(), hero_.scale(), hero_.isAlive, hero_.moving,
                       hero_.ridingOn != nullptr, hero_.hitBy != nullptr};
        heroReplaced_ = true;
        setupGame(character_);
        init();
    }

    timers_.update(kDt);
    if (profileClock) stepProfile.frame += int64_t(profileClock() - p3);
}

void Game::tickEngineOnly()
{
    map_->tick(ctx_, hero_);
    if (!hero_.moving) {
        hero_.moveOnEntity();
        hero_.moveOnCar();
        checkIfUserHasFallenOutOfFrame();
    }
    forwardScene();
}

void Game::forwardScene()
{
    Vec3 &w = world_->position;
    w.z -= (hero_.position().z - real(startingRow) + w.z) * cameraEasing;
    real targetCameraX = std::max(real(-3.0), std::min(real(2.0), -hero_.position().x));
    w.x += (targetCameraX - w.x) * cameraEasing;

    if (-w.z - camCount_ > real(1.0)) {
        camCount_ = -w.z;
        map_->newRow(ctx_);
    }
}

bool Game::isGameEnded() const { return !hero_.isAlive || state_ != GameState::Playing; }

void Game::checkIfUserHasFallenOutOfFrame()
{
    if (isGameEnded()) return;
    if (hero_.position().z < cameraPosition_.z - 1) {
        rumble();
        gameOver();
        playDeathSound();
    }
    if (hero_.position().x < -5 || hero_.position().x > 5) {
        rumble();
        gameOver();
        playDeathSound();
    }
}

void Game::gameOver()
{
    hero_.moving = false;
    hero_.stopAnimations(ctx_);
    setState(GameState::GameOver); // onGameEnded
}

void Game::updateScore()
{
    int position = std::max(int(rfloor(hero_.position().z)) - 8, 0);
    if (score_ < position) setScore(position); // onUpdateScore compares with the committed score
    // O11.3 Progression: the hero landed on the finish line - the level is done, the hero alive. Called from the
    // hop's own onComplete, so the animations are left alone (killing them here would run inside the GSAP tick).
    if (level_ > 0 && !levelDone_ && position >= levelRows()) {
        levelDone_ = true;
        hero_.moving = false;
        // O14: three fanfares, picked at random, so finishing level after level does not repeat one jingle
        sounds_.push_back("fanfare_" + toString(int(rfloor(rng_.fx.next() * 3))));
        setState(GameState::GameOver);
    }
}

void Game::onCollide(const Collision &c)
{
    if (isGameEnded()) return;
    hero_.isAlive = false;
    hero_.stopIdle(ctx_);
    real direction = c.hasSpeed ? c.obstacleSpeed : real(0);
    std::string type = c.type;
    auto finish = [this, type, direction]() {
        useParticle(type == "water" ? "water" : "feathers", direction);
        rumble();
        gameOver();
    };
    if (std::string(c.kind) == "car") {
        playCarHitSound();
        playDeathSound();
        finish();
    } else if (std::string(c.kind) == "train") {
        sounds_.push_back("train_die_0");
        // `await AudioManager.playAsync(...)`: the rest runs as a microtask after this frame's tick
        microtasks_.push_back([this, finish]() {
            playDeathSound();
            finish();
        });
    } else {
        finish();
    }
}

void Game::rumble()
{
    // Vibration.vibrate() has no R36S equivalent; the camera shake stays
    gsap_.to(&scene_->position, {{'x', 0}, {'y', 0}, {'z', 1}}, 0.2);
    gsap::Vars back;
    back.delay = 0.2;
    gsap_.to(&scene_->position, {{'x', 0}, {'y', 0}, {'z', 0}}, 0.2, back);
}

void Game::useParticle(const char *type, real direction)
{
    std::string t = type;
    nextFrame_.push_back([this, t, direction]() {
        if (t == "water") {
            water_.mesh->position = hero_.position();
            runWater();
            sounds_.push_back("water");
        } else if (t == "feathers") {
            feathers_.mesh->position = hero_.position();
            runFeathers(direction);
        }
    });
}

void Game::runFeathers(real direction)
{
    Rng &r = rng_.fx;
    const real explosionSpeed = 0.3;
    for (Node *p : feathers_.parts) {
        real m = direction < 0 ? real(-1) : real(1);
        real tx = (r.next() * real(5.0) + 2) * m;
        real ty = r.next() * real(2.0) + 1;
        real tz = r.next() * real(2.0) + 1;
        p->position.set(0, 0, 0);
        p->scale.set(1, 1, 1);
        p->visible = true;
        real delay = explosionSpeed + r.next() * real(0.5);

        const Vec3 values[4] = {{0, 0, 0},
                                {tx * real(0.25), ty * real(0.25), tz * real(0.25)},
                                {tx * real(0.5), ty * real(0.5), tz * real(0.5)},
                                {tx, ty, tz}};
        gsap_.bezierTo(&p->position, values, delay * 5);
        real rz = r.next() * (JS_PI * 2) + real(0.2);
        real rx = r.next() * (JS_PI * 2) + real(0.2);
        real ry = r.next() * (JS_PI * 2) + real(0.2);
        gsap::Vars ro;
        ro.delay = delay;
        gsap_.to(&p->rotation, {{'z', rz}, {'x', rx}, {'y', ry}}, delay * 5, ro);
        const real scaleTo = 0.01;
        gsap::Vars so;
        so.delay = delay * 3;
        so.onComplete = [p]() { p->visible = false; };
        gsap_.to(&p->scale, {{'x', scaleTo}, {'y', scaleTo}, {'z', scaleTo}}, delay, so);
    }
}

void Game::runWater()
{
    Rng &r = rng_.fx;
    const real explosionSpeed = 0.3;
    for (Node *p : water_.parts) {
        real tx = real(-1.0) + r.next() * real(1.0);
        real ty = r.next() * real(2.0) + 1;
        real tz = real(-1.0) + r.next() * real(1.0);
        real ex = tx * (r.next() * real(0.5) + real(1.1));
        real ez = tz * (r.next() * real(0.5) + real(1.1));
        p->position.set(0, 0, 0);
        p->scale.set(1, 1, 1);
        p->visible = true;
        real s = explosionSpeed + r.next() * real(0.5);
        const Vec3 values[4] = {{0, 0, 0},
                                {tx, ty, tz},
                                {tx * real(0.8), ty * real(0.8), tz * real(0.8)},
                                {ex, 0, ez}};
        gsap_.bezierTo(&p->position, values, s * 4, gsap::Vars().withEase(gsap::BounceOut()));
        const real scaleTo = 0.01;
        gsap::Vars so;
        so.delay = s * 3;
        so.onComplete = [p]() { p->visible = false; };
        gsap_.to(&p->scale, {{'x', scaleTo}, {'y', scaleTo}, {'z', scaleTo}}, s, so);
    }
}

// ------------------------------------------------------------------ AudioManager.ts

// O14/O15: only the chicken clucks - every other character squeaks with the beaver's voice, because a hen is wrong
// for a pig, an avocado or a beaver. The chicken keeps the original's 12 sounds in the original's order, so the
// traces against the original stay comparable.
static bool hasOwnVoice(const std::string &character) { return character != "chicken"; }

void Game::playMoveSound()
{
    const bool own = hasOwnVoice(character_);
    const int count = own ? 2 : 12; // O15: two beaver hops (the user's own recordings), twelve clucks
    const std::string prefix = own ? "beaver_move_" : "chicken_move_";
    // O15 (the game): the original calls out on every single hop, which wears the ear down after a minute. The game
    // speaks up about once in every two and four fifths hops, and picks the take at random instead of cycling.
    // (O16: was one in 3.5, which the user found too sparse.)
    if (!ctx_.originalBehaviour) {
        if (rng_.fx.next() >= real(1) / real(2.8)) return;
        sounds_.push_back(prefix + toString(int(rfloor(rng_.fx.next() * real(count)))));
        return;
    }
    sounds_.push_back(prefix + toString(audioFileMoveIndex_ % count));
    audioFileMoveIndex_ = (audioFileMoveIndex_ + 1) % count;
}

void Game::playPassiveCarSound()
{
    if (int(rfloor(rng_.fx.next() * 2)) == 0) sounds_.push_back("car_passive_1");
}

void Game::playDeathSound()
{
    const std::string prefix = hasOwnVoice(character_) ? "beaver_die_" : "chicken_die_";
    sounds_.push_back(prefix + toString(int(rfloor(rng_.fx.next() * 2))));
}

void Game::playCarHitSound() { sounds_.push_back("car_die_" + toString(int(rfloor(rng_.fx.next() * 2)))); }

// ------------------------------------------------------------------ input

void Game::beginMoveWithDirection()
{
    if (isGameEnded()) return;
    hero_.runPosieAnimation(ctx_);
}

void Game::moveWithDirection(Swipe direction)
{
    if (isGameEnded()) return;
    Player &h = hero_;
    h.ridingOn = nullptr;

    if (!h.initialPosition) {
        h.initialPosition = std::make_shared<Vec3>(h.position());
        h.targetPosition = h.initialPosition;
    }

    // the game (GameContext::originalBehaviour): every hop stops the previous hop's animations, whose end would otherwise
    // clear `moving` in the middle of this hop - also when `moving` is already false (a hop blocked by a tree clears it
    // at once while its bounce still runs)
    if (!ctx_.originalBehaviour) h.stopAnimations(ctx_);
    const bool interrupted = h.moving; // a hop cut short: skipPendingMovement drops the hero where it was heading
    h.skipPendingMovement();
    // O12 (the game): water only drowns a hero that stands still. In the original the previous hop's animations end in
    // the middle of the next one and clear `moving`, so the river still catches a hero hopping across it; the game
    // stops those animations (O4.1), and a chain of fast hops carried the hero over a whole river row without ever
    // touching it (user report after v017, the same kind of hole as the train in O10). A hop that starts from a water
    // row with nothing to stand on drowns the hero here instead of carrying on.
    if (interrupted && !ctx_.originalBehaviour) {
        const RowRef *landed = map_->getRow(jsRound(h.position().z));
        if (landed && landed->type == RowType::Water && !landed->water->getRidableForPosition(h.position())) {
            Collision c;
            c.type = "water";
            ctx_.onCollide(c);
            return;
        }
    }

    Vec3 velocity{0, 0, 0};
    h.targetRotation = normalizeAngle(h.rotation().y);
    const Vec3 initial = *h.initialPosition;

    auto roundX = [&h]() {
        h.targetPosition->x = jsRound(h.targetPosition->x);
        if (h.ridingOn && h.ridingOn->dir) {
            if (h.ridingOn->dir < 0) h.targetPosition->x = rfloor(h.targetPosition->x);
            else if (h.ridingOn->dir > 0) h.targetPosition->x = rceil(h.targetPosition->x);
            else h.targetPosition->x = jsRound(h.targetPosition->x);
        }
    };

    switch (direction) {
    case Swipe::Left:
        h.targetRotation = 3.141592653589793 * 0.5;
        velocity = {1, 0, 0};
        h.targetPosition = std::make_shared<Vec3>(initial.x + 1, initial.y, initial.z);
        h.moving = true;
        break;
    case Swipe::Right:
        // `if (this._hero.targetPosition === 0)` can never be true in the original
        if (int(h.targetRotation) != -int(PI_2) && int(h.targetRotation) != int(real(3.141592653589793) + PI_2))
            h.targetRotation = 3.141592653589793 + 3.141592653589793 * 0.5;
        velocity = {-1, 0, 0};
        h.targetPosition = std::make_shared<Vec3>(initial.x - 1, initial.y, initial.z);
        h.moving = true;
        break;
    case Swipe::Up: {
        h.targetRotation = 0;
        const RowRef *row = map_->getRow(initial.z);
        if (row && row->type == RowType::Road) playPassiveCarSound();
        velocity = {0, 0, 1};
        h.targetPosition = std::make_shared<Vec3>(initial.x, initial.y, initial.z + 1);
        roundX();
        h.moving = true;
        break;
    }
    case Swipe::Down:
        h.targetRotation = 3.141592653589793;
        velocity = {0, 0, -1};
        h.targetPosition = std::make_shared<Vec3>(initial.x, initial.y, initial.z - 1);
        roundX();
        h.moving = true;
        break;
    }

    if (map_->treeCollision(*h.targetPosition)) {
        h.targetPosition = std::make_shared<Vec3>(initial.x, initial.y, initial.z);
        h.moving = false;
    }

    const RowRef *targetRow = map_->getRow(initial.z + velocity.z);
    // `targetRow.entity.top || groundLevel`: with no row the original throws here (never happens in play)
    real finalY = targetRow && targetRow->top() != 0 ? targetRow->top() : groundLevel;
    if (targetRow && targetRow->type == RowType::Water) {
        RowEntity *ridable = targetRow->water->getRidableForPosition(*h.targetPosition);
        finalY = ridable ? targetRow->water->getPlayerLowerBouncePositionForEntity(*ridable)
                         : targetRow->water->getPlayerSunkenPosition();
    }

    playMoveSound();
    h.targetPosition->y = finalY;
    h.commitMovementAnimations(ctx_, [this]() { updateScore(); });
}

} // namespace cr
