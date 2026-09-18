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
    for (int i = 0; i < 2; i++) {
        heroes_[i] = Player();
        heroes_[i].index = i;
    }
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
    for (int i = 0; i < playerCount_; i++) {
        heroes_[i].construct(ctx_, characterOf(i));
        world_->add(heroes_[i].object);
    }

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

void Game::setScore(int player, int s)
{
    pendingScore_[player] = true;
    nextScore_[player] = s;
}

// O23: the second player's animal. The app may set it; otherwise it is whichever of the game's two own characters
// the first player is not using, so a two-player game never shows the same animal twice.
const std::string &Game::characterOf(int player) const
{
    if (player == 0) return character_;
    // never the same animal twice: a cached choice that the first player has since taken is recomputed
    if (character2_.empty() || character2_ == character_)
        character2_ = character_ == "beaver" ? std::string("chicken") : std::string("beaver");
    return character2_;
}

int Game::leader() const
{
    int best = -1;
    for (int i = 0; i < playerCount_; i++)
        if (heroes_[i].isAlive && (best < 0 || heroes_[i].position().z > heroes_[best].position().z)) best = i;
    return best < 0 ? 0 : best;
}

int Game::winner() const
{
    if (playerCount_ < 2) return 0;
    if (score_[0] == score_[1]) return -1;
    return score_[0] > score_[1] ? 0 : 1;
}

bool Game::playerBlocked(int player) const
{
    // a player this game does not have is always blocked: nothing built its hero, so nothing may touch it
    if (player < 0 || player >= playerCount_) return true;
    return state_ != GameState::Playing || !heroes_[player].isAlive;
}

void Game::commitState()
{
    for (int i = 0; i < 2; i++)
        if (pendingScore_[i]) score_[i] = nextScore_[i];
    if (pendingState_) {
        state_ = nextState_;
        // ScoreText: useEffect on gameOver -> setHighscore(score) when it is a new best (the best score belongs to
        // Classic: a Progression level ends at its finish line, so its score says nothing about how far one gets)
        // O23: and to a game of one - a duel is a different game, and its winner's score is not this game's record.
        if (state_ == GameState::GameOver && level_ == 0 && playerCount_ == 1 && score_[0] > highscore_)
            highscore_ = score_[0];
    }
    pendingState_ = false;
    pendingScore_[0] = pendingScore_[1] = false;
}

void Game::init()
{
    setScore(0, 0); // onGameInit
    if (playerCount_ > 1) setScore(1, 0);
    cameraPosition_.z = 1;
    for (int i = 0; i < playerCount_; i++) {
        Player &h = heroes_[i];
        h.reset();
        h.carriedBy = h.carrying = nullptr;
        h.respawnSteps = h.warnSteps = 0;
        // O23: two players start side by side, on the columns either side of the middle, so neither of them is
        // standing on the other before the game has even begun
        if (playerCount_ > 1) h.position().x = i == 0 ? real(-1) : real(1);
    }
    // O11.3: the finish line of this level, at the row the score `levelRows()` is reached on
    levelDone_ = false;
    map_->finishRow = level_ > 0 ? startingRow + levelRows() : 0;

    // scene.resetParticles(hero.position)
    feathers_.mesh->position = heroes_[0].position();
    water_.mesh->position = heroes_[0].position();
    feathers_.mesh->position.y = 0;
    water_.mesh->position.y = 0;

    camCount_ = 0;
    map_->reset();
    for (int i = 0; i < playerCount_; i++) heroes_[i].idle(ctx_);
    map_->init(ctx_);
}

void Game::startPlaying()
{
    // updateWithGameState(playing)
    if (state_ == GameState::Playing) return;
    GameState last = state_;
    setState(GameState::Playing);
    if (last == GameState::None) {
        for (int i = 0; i < playerCount_; i++) {
            heroes_[i].stopIdle(ctx_);
            // onSwipe(SWIPE_UP): ignored, React has not committed "playing" yet, so the first press never hops
            moveWithDirection(Swipe::Up, i);
        }
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
    setScore(0, 0);
    if (playerCount_ > 1) setScore(1, 0);
    init();
}

void Game::quitToHome()
{
    if (state_ != GameState::Playing) return;
    for (int i = 0; i < playerCount_; i++) {
        heroes_[i].moving = false;
        heroes_[i].stopAnimations(ctx_);
    }
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

void Game::setPlayerCount(int count)
{
    const int n = count < 1 ? 1 : count > 2 ? 2 : count;
    if (n == playerCount_) return;
    playerCount_ = n;
    // every row must offer two ways through now (or one again), which changes the map from the first row on
    ctx_.twoPaths = n > 1;
    setupGame(character_);
    init();
}

void Game::setCharacter(const std::string &characterId)
{
    character_ = characterId;
    if (character2_ == characterId) character2_.clear(); // let the other player pick the opposite again
    heroes_[0].setCharacter(ctx_, characterId);
    for (int i = 1; i < playerCount_; i++) heroes_[i].setCharacter(ctx_, characterOf(i));
}

void Game::setCharacter(int player, const std::string &characterId)
{
    if (player <= 0) {
        setCharacter(characterId);
        return;
    }
    character2_ = characterId;
    if (player < playerCount_) heroes_[player].setCharacter(ctx_, characterId);
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
    map_->tick(ctx_, heroes_, playerCount_);
    const uint64_t p2 = profileClock ? profileClock() : 0;
    for (int i = 0; i < playerCount_; i++) {
        Player &h = heroes_[i];
        if (!h.moving) {
            h.moveOnEntity();
            h.moveOnCar();
            checkIfUserHasFallenOutOfFrame(i);
        }
    }
    if (playerCount_ > 1) {
        updateCarrying();
        updateGap();
        updateRespawn();
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
        heroBefore_ = {heroes_[0].position(), heroes_[0].rotation(), heroes_[0].scale(), heroes_[0].isAlive,
                       heroes_[0].moving, heroes_[0].ridingOn != nullptr, heroes_[0].hitBy != nullptr};
        heroReplaced_ = true;
        setupGame(character_);
        init();
    }

    timers_.update(kDt);
    if (profileClock) stepProfile.frame += int64_t(profileClock() - p3);
}

void Game::tickEngineOnly()
{
    map_->tick(ctx_, heroes_, playerCount_);
    for (int i = 0; i < playerCount_; i++) {
        Player &h = heroes_[i];
        if (!h.moving) {
            h.moveOnEntity();
            h.moveOnCar();
            checkIfUserHasFallenOutOfFrame(i);
        }
    }
    forwardScene();
}

void Game::forwardScene()
{
    Vec3 &w = world_->position;
    real followZ = heroes_[0].position().z, followX = heroes_[0].position().x;
    if (playerCount_ > 1) {
        // O23: the camera has to hold BOTH players. It follows the leader but hangs back half of the gap between
        // them, so neither is ever off the bottom edge while the gap is inside kMaxGap - which is exactly the gap
        // the duel and the co-op pull-back are measured against. Sideways it sits between the two columns.
        const int lead = leader(), other = 1 - lead;
        followZ = heroes_[lead].position().z;
        followX = heroes_[lead].position().x;
        if (heroes_[other].isAlive) {
            real gap = followZ - heroes_[other].position().z;
            if (gap < real(0)) gap = real(0);
            if (gap > real(kMaxGap)) gap = real(kMaxGap);
            followZ -= gap / real(2);
            followX = (heroes_[0].position().x + heroes_[1].position().x) / real(2);
        }
    }
    w.z -= (followZ - real(startingRow) + w.z) * cameraEasing;
    real targetCameraX = std::max(real(-3.0), std::min(real(2.0), -followX));
    w.x += (targetCameraX - w.x) * cameraEasing;

    if (-w.z - camCount_ > real(1.0)) {
        camCount_ = -w.z;
        map_->newRow(ctx_);
    }
    // O23: the camera hanging back means the rows it asks for arrive later, and the leader can walk off the end of
    // the map. Rows are cheap and pooled, so the leader gets its own margin on top of the camera's.
    if (playerCount_ > 1) {
        const real needed = heroes_[leader()].position().z + real(14);
        while (real(map_->rowCount) < needed) map_->newRow(ctx_);
    }
}

// O23: over when nobody is left. With one player this is the original's `!hero.isAlive || state != playing`.
bool Game::isGameEnded() const
{
    if (state_ != GameState::Playing) return true;
    for (int i = 0; i < playerCount_; i++)
        if (heroes_[i].isAlive) return false;
    return true;
}

void Game::checkIfUserHasFallenOutOfFrame(int player)
{
    if (playerBlocked(player)) return;
    Player &h = heroes_[player];
    if (h.position().z < cameraPosition_.z - 1) outOfFrame(player);
    if (h.position().x < -5 || h.position().x > 5) outOfFrame(player);
}

// Behind the camera, or off the side. The original neither marks the hero dead nor throws feathers here, and both
// tests can fire in the same step - kept exactly, because the traces against the original show it.
void Game::outOfFrame(int player)
{
    rumble();
    if (playerCount_ > 1) {
        heroes_[player].isAlive = false; // the other player carries on
        endForPlayer(player);
    } else {
        gameOver();
    }
    playDeathSound(player);
}

void Game::gameOver()
{
    for (int i = 0; i < playerCount_; i++) {
        heroes_[i].moving = false;
        heroes_[i].stopAnimations(ctx_);
    }
    setState(GameState::GameOver); // onGameEnded
}

// This player is finished: its animations stop, whoever it was carrying comes down, and the GAME ends only if no
// player is left standing. With one player that is exactly what gameOver() did.
void Game::endForPlayer(int player)
{
    Player &h = heroes_[player];
    h.moving = false;
    h.stopAnimations(ctx_);
    if (h.carrying) {
        h.carrying->carriedBy = nullptr;
        h.carrying = nullptr;
    }
    if (h.carriedBy) {
        h.carriedBy->carrying = nullptr;
        h.carriedBy = nullptr;
    }
    // Progression is co-operative: a dead player comes back on the partner's head a couple of seconds later
    if (playerCount_ > 1 && level_ > 0) h.respawnSteps = settings::respawnSteps;
    if (isGameEnded()) setState(GameState::GameOver); // onGameEnded
}

// A death the game itself decides on (the duel's gap), with the particles and the sound a collision would give.
void Game::killPlayer(int player, const char *particle, real direction)
{
    Player &h = heroes_[player];
    if (!h.isAlive) return;
    h.isAlive = false;
    h.stopIdle(ctx_);
    useParticle(particle, direction, player);
    rumble();
    playDeathSound(player);
    endForPlayer(player);
}

void Game::updateScore(int player)
{
    Player &h = heroes_[player];
    int position = std::max(int(rfloor(h.position().z)) - 8, 0);
    if (score_[player] < position) setScore(player, position); // onUpdateScore compares with the committed score
    // O11.3 Progression: the hero landed on the finish line - the level is done, the hero alive. Called from the
    // hop's own onComplete, so the animations are left alone (killing them here would run inside the GSAP tick).
    if (level_ > 0 && !levelDone_ && position >= levelRows()) {
        levelDone_ = true;
        h.moving = false;
        // O14: three fanfares, picked at random, so finishing level after level does not repeat one jingle
        sounds_.push_back("fanfare_" + toString(int(rfloor(rng_.fx.next() * 3))));
        setState(GameState::GameOver);
    }
}

void Game::onCollide(const Collision &c)
{
    // O23: WHICH player was hit (null means the first, so every 1:1 call site is unchanged)
    const int pi = c.who ? c.who->index : 0;
    if (playerBlocked(pi)) return;
    Player &victim = heroes_[pi];
    victim.isAlive = false;
    victim.stopIdle(ctx_);
    real direction = c.hasSpeed ? c.obstacleSpeed : real(0);
    std::string type = c.type;
    auto finish = [this, type, direction, pi]() {
        useParticle(type == "water" ? "water" : "feathers", direction, pi);
        rumble();
        endForPlayer(pi);
    };
    if (std::string(c.kind) == "car") {
        playCarHitSound();
        playDeathSound(pi);
        finish();
    } else if (std::string(c.kind) == "train") {
        sounds_.push_back("train_die_0");
        // `await AudioManager.playAsync(...)`: the rest runs as a microtask after this frame's tick
        microtasks_.push_back([this, finish, pi]() {
            playDeathSound(pi);
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

void Game::useParticle(const char *type, real direction, int player)
{
    std::string t = type;
    nextFrame_.push_back([this, t, direction, player]() {
        if (t == "water") {
            water_.mesh->position = heroes_[player].position();
            runWater();
            sounds_.push_back("water");
        } else if (t == "feathers") {
            feathers_.mesh->position = heroes_[player].position();
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

void Game::playMoveSound(int player)
{
    const bool own = hasOwnVoice(characterOf(player));
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

void Game::playDeathSound(int player)
{
    const std::string prefix = hasOwnVoice(characterOf(player)) ? "beaver_die_" : "chicken_die_";
    sounds_.push_back(prefix + toString(int(rfloor(rng_.fx.next() * 2))));
}

void Game::playCarHitSound() { sounds_.push_back("car_die_" + toString(int(rfloor(rng_.fx.next() * 2)))); }

// ------------------------------------------------------------------ input

void Game::beginMoveWithDirection(int player)
{
    if (playerBlocked(player)) return;
    heroes_[player].runPosieAnimation(ctx_);
}

void Game::moveWithDirection(Swipe direction, int player)
{
    if (playerBlocked(player)) return;
    Player &h = heroes_[player];
    h.ridingOn = nullptr;
    // O23: hopping off the other player's head - from here on this is an ordinary hop from that tile
    if (h.carriedBy) {
        h.carriedBy->carrying = nullptr;
        h.carriedBy = nullptr;
    }

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

    // O23: THE OTHER PLAYER IS STANDING THERE - this hop lands on its head instead of on the ground. The upper
    // one then rides along with it (updateCarrying) until one of them hops away, which is what the user asked for:
    // "jesli jeden na drugiego pole wskoczy to jeden wskakuje na glowe drugiemu, a jak ten sie rusza to ucieka".
    if (playerCount_ > 1) {
        Player &other = heroes_[player == 0 ? 1 : 0];
        if (other.isAlive && !other.moving && !other.carriedBy &&
            jsRound(other.position().z) == jsRound(h.targetPosition->z) &&
            rabs(other.position().x - h.targetPosition->x) < real(0.5)) {
            h.targetPosition->x = other.position().x;
            finalY = other.position().y + real(settings::headHeight);
            h.carriedBy = &other;
            other.carrying = &h;
        }
    }

    playMoveSound(player);
    h.targetPosition->y = finalY;
    h.commitMovementAnimations(ctx_, [this, player]() { updateScore(player); });
}

// ------------------------------------------------------------------ two players (O23)
//
// None of this exists in the original - the upstream game has a multiplayer BUTTON on its home screen wired to an
// empty function and nothing behind it. These are the user's rules, written down in docs/PLAN_2PLAYERS.md.

// One player stands on the other's head and rides along with it. The moment the lower one hops away it "escapes and
// leaves the other behind", so the two can play independently again.
void Game::updateCarrying()
{
    for (int i = 0; i < playerCount_; i++) {
        Player &h = heroes_[i];
        Player *under = h.carriedBy;
        if (!under) continue;
        if (under->moving || !under->isAlive) {
            h.carriedBy = nullptr;
            under->carrying = nullptr;
            landAfterCarry(i);
            continue;
        }
        h.position().x = under->position().x;
        h.position().z = under->position().z;
        h.position().y = under->position().y + real(settings::headHeight);
        if (h.initialPosition) {
            h.initialPosition->x = h.position().x;
            h.initialPosition->y = h.position().y;
            h.initialPosition->z = h.position().z;
        }
    }
    stackOnOneTile();
}

// The two can still end up sharing a tile without either being carried: a hop blocked by a tree keeps a player
// where it was, and both can land on the same square in the same step. Whoever is higher goes on top, so they are
// never drawn inside one another. Found by build/two_player_check.sh, not by looking at the screen.
void Game::stackOnOneTile()
{
    Player &a = heroes_[0], &b = heroes_[1];
    if (!a.isAlive || !b.isAlive) return;
    if (a.moving || b.moving || a.carriedBy || b.carriedBy) return;
    if (jsRound(a.position().z) != jsRound(b.position().z)) return;
    if (rabs(a.position().x - b.position().x) >= real(0.5)) return;
    const int top = a.position().y >= b.position().y ? 0 : 1;
    Player &upper = heroes_[top], &lower = heroes_[top == 0 ? 1 : 0];
    upper.carriedBy = &lower;
    lower.carrying = &upper;
    upper.position().x = lower.position().x;
    upper.position().z = lower.position().z;
    upper.position().y = lower.position().y + real(settings::headHeight);
    if (upper.initialPosition) *upper.initialPosition = upper.position();
}

void Game::landAfterCarry(int player)
{
    Player &h = heroes_[player];
    if (!h.isAlive) return;
    // the same choice of height a hop makes (Game::moveWithDirection): the row's top, or the water's log or surface
    const RowRef *row = map_->getRow(jsRound(h.position().z));
    real y = row && row->top() != real(0) ? row->top() : real(groundLevel);
    if (row && row->type == RowType::Water) {
        RowEntity *ridable = row->water->getRidableForPosition(h.position());
        y = ridable ? row->water->getPlayerLowerBouncePositionForEntity(*ridable)
                    : row->water->getPlayerSunkenPosition();
    }
    if (h.initialPosition) h.initialPosition->y = y;
    ctx_.gsap->to(&h.position(), {{'y', y}}, real(0.15));
}

// How far apart the two may get. Classic is a duel, so the one left behind dies; Progression is co-operative, so
// the leader is pulled back onto the other one's head instead.
void Game::updateGap()
{
    if (state_ != GameState::Playing) return;
    Player &a = heroes_[0], &b = heroes_[1];
    if (!a.isAlive || !b.isAlive) return;
    if (a.carriedBy || b.carriedBy) return; // one is standing on the other: there is no gap
    const int lead = a.position().z >= b.position().z ? 0 : 1;
    Player &front = heroes_[lead], &back = heroes_[lead == 0 ? 1 : 0];
    const real gap = front.position().z - back.position().z;
    if (gap < real(kMaxGap)) {
        a.warnSteps = b.warnSteps = 0;
        return;
    }
    // at the limit the trailing player blinks (the renderers read warnSteps); past it the rule applies
    back.warnSteps++;
    front.warnSteps = 0;
    if (gap <= real(kMaxGap)) return;
    if (level_ == 0) killPlayer(back.index, "feathers", real(0));
    else pullBack(front.index, back.index);
}

void Game::pullBack(int front, int back)
{
    Player &f = heroes_[front], &b = heroes_[back];
    f.moving = false;
    f.stopAnimations(ctx_);
    ctx_.gsap->killTweensOf(&f.position());
    f.ridingOn = nullptr;
    f.hitBy = nullptr;
    f.position().set(b.position().x, b.position().y + real(settings::headHeight), b.position().z);
    f.initialPosition = std::make_shared<Vec3>(f.position());
    f.targetPosition = f.initialPosition;
    f.carriedBy = &b;
    b.carrying = &f;
    f.warnSteps = b.warnSteps = 0;
    sounds_.push_back("banner");
}

void Game::updateRespawn()
{
    if (level_ == 0 || state_ != GameState::Playing) return;
    for (int i = 0; i < playerCount_; i++) {
        Player &h = heroes_[i];
        if (h.isAlive || h.respawnSteps <= 0) continue;
        const int partner = i == 0 ? 1 : 0;
        if (!heroes_[partner].isAlive) continue; // nothing to come back to; the game has ended anyway
        if (--h.respawnSteps == 0) reviveOnPartner(i, partner);
    }
}

void Game::reviveOnPartner(int player, int partner)
{
    Player &h = heroes_[player], &p = heroes_[partner];
    h.stopAnimations(ctx_);
    // the death tweens were started on the scale and rotation directly, so pausing the hop animations is not enough
    ctx_.gsap->killTweensOf(&h.position());
    ctx_.gsap->killTweensOf(&h.rotation());
    ctx_.gsap->killTweensOf(&h.scale());
    h.isAlive = true;
    h.moving = false;
    h.hitBy = nullptr;
    h.ridingOn = nullptr;
    h.respawnSteps = 0;
    h.warnSteps = 0;
    h.scale().set(1, 1, 1);
    h.rotation().set(0, 0, 0);
    h.targetRotation = 0;
    h.position().set(p.position().x, p.position().y + real(settings::headHeight), p.position().z);
    h.initialPosition = std::make_shared<Vec3>(h.position());
    h.targetPosition = h.initialPosition;
    h.carriedBy = &p;
    p.carrying = &h;
    sounds_.push_back("banner");
}

} // namespace cr
