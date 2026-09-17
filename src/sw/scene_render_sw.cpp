#include "scene_render_sw.h"

#include <algorithm>

#include "engine/assets.h"
#include "engine/log.h"
#include "game/settings.h"

namespace cr {

using namespace settings;

// Shadowed top faces keep only the ambient term: ((1.8/PI) / ((1.8 + N.L)/PI))^(1/2.2) with N = up, which is the
// Lambert table's light at n.l = 0 over its light at n.l = light.y (no pow on the SF2000)
static mreal shadowFactorForTopFaces()
{
    const Vec3 l = normalize({lightX, lightY, lightZ});
    const int64_t lit = sw::lambertQ16(l.y.v), ambient = sw::lambertQ16(0);
    return Fixed::fromRaw(int32_t((ambient << 16) / lit));
}

bool SceneRenderer::init(Renderer &renderer, ModelLibrary &models, const Manifest &manifest, const std::string &dataDir)
{
    for (const auto &entry : manifest.models) {
        FlatMeshData fm;
        const std::string path = dataDir + "meshes/" + entry.first + ".fmesh";
        if (!loadFlatMesh(path, fm)) {
            logf("scene: cannot load %s", path.c_str());
            return false;
        }
        meshes_[entry.first] = renderer.uploadMesh(fm);
        if (Model *m = models.find(entry.first)) {
            m->mesh = &meshes_[entry.first];
            m->texture = &texture_;
        }
    }
    logf("scene: %d flat meshes uploaded", int(meshes_.size()));
    return true;
}

// height of the surface shadows fall on, in model space: the top of the floor box, except the railroad whose ground is
// at 0.25 under ties and rails
static real floorTop(const Model *m)
{
    return m->name == "railroad" ? real(0.25) : m->aabbMax.y;
}

static bool isStaticObstacle(const Model *m)
{
    return m->name.compare(0, 4, "tree") == 0 || m->name.compare(0, 7, "boulder") == 0;
}

// O7.4: rsqrt is a 64-bit bit-by-bit loop; the model sizes and node scales it is asked for repeat, so the results are
// kept, keyed by the exact input
static real cachedSqrt(real x)
{
    struct Entry {
        int32_t in, out;
        bool used;
    };
    static Entry cache[128];
    Entry &e = cache[(uint32_t(x.v) * 2654435761u) >> 25];
    if (e.used && e.in == x.v) return real::fromRaw(e.out);
    const real r = rsqrt(x);
    e = {x.v, r.v, true};
    return r;
}

static real maxScale(const Mat4 &m)
{
    const real sx = m.e[0] * m.e[0] + m.e[1] * m.e[1] + m.e[2] * m.e[2];
    const real sy = m.e[4] * m.e[4] + m.e[5] * m.e[5] + m.e[6] * m.e[6];
    const real sz = m.e[8] * m.e[8] + m.e[9] * m.e[9] + m.e[10] * m.e[10];
    real s = sx > sy ? sx : sy;
    if (sz > s) s = sz;
    return cachedSqrt(s);
}

void SceneRenderer::drawNode(Renderer &renderer, Node *node, real planeY)
{
    if (!node->visible || rowOutside(node)) return; // an unseen row has no fresh world matrices (updateVisibleWorld)

    const Model *model = node->model;
    const bool wasInHero = inHero_, wasPlaneKnown = planeKnown_;
    const int64_t wasFloorFar = floorFar_;
    const size_t itemsBegin = objects_.size(); // O16: where this subtree's items start
    if (node == hero_) {
        planeY = heroPlane_;
        inHero_ = true;
        planeKnown_ = true;
    }
    const bool hasMesh = model && model->mesh;
    if (hasMesh || node->shape != Shape::None) {
        const Vec3 mn = hasMesh ? model->aabbMin : Vec3{real(-0.5), real(-0.5), real(-0.5)};
        const Vec3 mx = hasMesh ? model->aabbMax : Vec3{real(0.5), real(0.5), real(0.5)};
        Mat4 world = node->world;
        if (node->shape != Shape::None) world = mulAffine(world, composeEuler({0, 0, 0}, {0, 0, 0}, node->shapeSize));
        world.e[12] -= origin_.x;
        world.e[13] -= origin_.y;
        world.e[14] -= origin_.z;
        const Vec3 center = world.transformPoint((mn + mx) * real(0.5));
        const Vec3 size = mx - mn;
        const real radius = cachedSqrt(dot(size, size)) * real(0.5) * maxScale(world); // length(mx - mn)
        const Vec3 clip = viewProjRelative_.transformPoint(center);
        // shadows reach ~1.4 units beyond their caster, so casters are kept a little longer (for their shadow only
        // without a depth buffer: a caster itself is drawn with the margin of everything else)
        const auto outsideBy = [&](real margin) {
            return rabs(clip.x) > real(1) + radius * rabs(projection.e[0]) * margin ||
                   rabs(clip.y) > real(1) + radius * rabs(projection.e[5]) * margin;
        };
        const bool outside = outsideBy(node->castShadow && depthBuffer ? real(2.5) : real(1.5));
        const bool casts = hasMesh && node->castShadow && !outsideBy(real(2.5));
        if (outside && !casts) culled++;
        if (!outside) {
            const bool floor = hasMesh && model->receiveShadow && !model->castShadow;
            Item item{node, world, 0, hasMesh && planeKnown_ && !floor, planeY - origin_.y};
            if (depthBuffer) {
                drawItem(renderer, item);
            } else {
                // distance along the view: the camera looks down its -z
                item.far = -viewRelative_.transformPoint(center).z.v;
                if (floor) {
                    floorFar_ = item.far;
                    floors_.push_back(item);
                } else if (hasMesh && model->name == "lily_pad") {
                    // turning lily pads reach over the row's edge: drawn right after their water, before any nearer
                    // floor covers what sticks out (the depth buffer hid it under the grass)
                    item.far = floorFar_ - 1;
                    floors_.push_back(item);
                } else {
                    objects_.push_back(item);
                }
            }
        }
        if (casts) {
            // Simple: the caster's bounding box instead of its mesh (6 lit triangles at most)
            if (shadowMode == ShadowMode::Full)
                casters_.push_back({model->mesh, world, planeY - origin_.y});
            else if (shadowMode == ShadowMode::Simple)
                casters_.push_back({&renderer.unitBox,
                                    mulAffine(world, composeEuler((mn + mx) * real(0.5), {0, 0, 0}, mx - mn)),
                                    planeY - origin_.y});
        }
        // floors (receive, never cast) define the plane their entities' shadows fall on
        if (model && model->receiveShadow && !model->castShadow) {
            planeY = node->world.e[13] + floorTop(model);
            planeKnown_ = true;
        }
    }
    for (Node *c : node->children) drawNode(renderer, c, planeY);
    if (node == hero_) heroBegin_ = itemsBegin, heroEnd_ = objects_.size();
    else if (node == ridingNode_) ridingBegin_ = itemsBegin, ridingEnd_ = objects_.size();
    inHero_ = wasInHero;
    planeKnown_ = wasPlaneKnown;
    floorFar_ = wasFloorFar;
}

void SceneRenderer::drawItem(Renderer &renderer, const Item &item)
{
    const Node *node = item.node;
    const Model *model = node->model;
    if (node->shape != Shape::None) {
        const bool plane = node->shape == Shape::Plane;
        renderer.drawFlat(plane ? renderer.unitPlane : renderer.unitBox, node->shapeColor, item.world, plane);
        drawsByKind[KindShape]++;
        return;
    }
    const int drawnBefore = renderer.stats.trianglesDrawn;
    renderer.clipBelow = item.clip && !depthBuffer;
    renderer.clipBelowY = item.clipY;
    // lily pads draw with the floors, before the shadows: their own shadow must not darken them
    renderer.shadowProof = !depthBuffer && shadowMode != ShadowMode::Off && model->name == "lily_pad";
    renderer.drawLambert(*model->mesh, *model->texture, item.world);
    renderer.clipBelow = false;
    renderer.shadowProof = false;
    if (drawLog) drawLog->push_back({model, renderer.stats.trianglesDrawn - drawnBefore});
    bool inHero = false;
    for (const Node *n = node; n; n = n->parent)
        if (n == hero_) inHero = true;
    if (inHero) drawsByKind[KindHero]++;
    else if (model->receiveShadow && !model->castShadow) drawsByKind[KindFloor]++;
    else if (isStaticObstacle(model)) drawsByKind[KindObstacle]++;
    else drawsByKind[KindEntity]++;
}

void SceneRenderer::drawFarToNear(Renderer &renderer, const std::vector<Item> &items, int64_t &drawTime)
{
    const uint64_t t0 = profileClock ? profileClock() : 0;
    order_.resize(items.size());
    for (size_t i = 0; i < items.size(); i++) order_[i] = {items[i].far, uint16_t(i)};
    // far to near; equal distances keep the scene order (what stable_sort gave)
    std::sort(order_.begin(), order_.end(),
              [](const std::pair<int64_t, uint16_t> &a, const std::pair<int64_t, uint16_t> &b) {
                  return a.first > b.first || (a.first == b.first && a.second < b.second);
              });
    const uint64_t t1 = profileClock ? profileClock() : 0;
    for (const auto &o : order_) drawItem(renderer, items[o.second]);
    if (profileClock) {
        profile.sort += int64_t(t1 - t0);
        drawTime += int64_t(profileClock() - t1);
    }
}

void SceneRenderer::drawShadows(Renderer &renderer)
{
    shadowCasters = int(casters_.size());
    drawsByKind[KindShadow] = shadowMode != ShadowMode::Off ? shadowCasters : 0;
    if (shadowMode != ShadowMode::Off && !casters_.empty()) {
        renderer.beginShadows(shadowFactorForTopFaces());
        for (const Caster &c : casters_) renderer.drawShadow(*c.mesh, c.world, c.planeY);
        renderer.endShadows();
    }
}

bool SceneRenderer::rowOutside(const Node *row) const
{
    if (row->parent != rowParent_ || row == hero_ || row == particlesA_ || row == particlesB_) return false;
    // Everything of a row lies within x -16..16 of it (logs and cars wrap at 11; a train further out is beside the
    // screen), y -1..5 and z -2..2 (tall trees, shadows): that box's extent in clip space against the screen edges.
    Mat4 world = row->world;
    world.e[12] -= origin_.x;
    world.e[13] -= origin_.y;
    world.e[14] -= origin_.z;
    const Mat4 m = mulAffine(viewProjRelative_, world);
    const Vec3 c = m.transformPoint({0, real(2), 0});
    const real hx = 16, hy = 3, hz = 2;
    const real ex = rabs(m.e[0]) * hx + rabs(m.e[4]) * hy + rabs(m.e[8]) * hz;
    const real ey = rabs(m.e[1]) * hx + rabs(m.e[5]) * hy + rabs(m.e[9]) * hz;
    return rabs(c.x) - ex > real(1) || rabs(c.y) - ey > real(1);
}

void SceneRenderer::updateVisibleWorld(Node *node, const Mat4 &parentWorld)
{
    node->world = mulAffine(parentWorld, node->localMatrix()); // updateWorld (scene.cpp)
    if (rowOutside(node)) {
        culledRows++;
        return;
    }
    for (Node *c : node->children) updateVisibleWorld(c, node->world);
}

void SceneRenderer::render(Renderer &renderer, Game &game, int width, int height, mreal viewScale)
{
    profile = Profile();
    // the camera first: it depends on the camera position alone, and the row test needs it
    setupCamera(game, width, height, viewScale);
    const uint64_t t0 = profileClock ? profileClock() : 0;
    rowParent_ = game.world();
    hero_ = game.hero().object;
    particlesA_ = game.feathers().mesh;
    particlesB_ = game.waterParticles().mesh;
    culledRows = 0;
    updateVisibleWorld(game.sceneRoot(), Mat4::identity());
    if (profileClock) profile.updateWorld = int64_t(profileClock() - t0);
    renderScene(renderer, game, height);
}

void SceneRenderer::setupCamera(Game &game, int width, int height, mreal viewScale)
{
    Mat4 camWorld = lookAtRotation({-1, real(2.8), real(-2.9)}, {0, 0, 0}, {0, 1, 0});
    viewRelative_ = inverseRigid(camWorld);
    origin_ = game.cameraPosition();
    camWorld.e[12] = origin_.x;
    camWorld.e[13] = origin_.y;
    camWorld.e[14] = origin_.z;
    view = inverseRigid(camWorld);
    const mreal w = mreal(width) * viewScale, h = mreal(height) * viewScale;
    projection = orthographic(-w, w, h, -h, cameraNear, cameraFar, cameraZoom);
    if (viewShift != 0) {
        Mat4 shift = Mat4::identity();
        shift.e[13] = -viewShift;
        projection = shift * projection;
    }
    viewProjRelative_ = projection * viewRelative_;
}

void SceneRenderer::renderScene(Renderer &renderer, Game &game, int height)
{
    Node *root = game.sceneRoot();

    renderer.setCamera(projection, viewRelative_);
    renderer.setLightDirection({lightX, lightY, lightZ});
    renderer.depthBuffer = depthBuffer;
    culled = 0;
    casters_.clear();
    floors_.clear();
    objects_.clear();
    for (int &k : drawsByKind) k = 0;

    // the hero is not a child of a row: its shadow falls on the surface under it
    Player &hero = game.hero();
    hero_ = hero.object;
    ridingNode_ = hero.ridingOn ? hero.ridingOn->mesh : nullptr;
    heroBegin_ = heroEnd_ = ridingBegin_ = ridingEnd_ = 0;
    heroPlane_ = hero.object->world.e[13] - hero.position().y; // world y of the row origin
    const RowRef *row = game.map().getRow(jsRound(hero.position().z));
    if (hero.ridingOn && hero.ridingOn->mesh->model)
        heroPlane_ = hero.ridingOn->mesh->world.e[13] + hero.ridingOn->mesh->model->aabbMax.y;
    else if (row) {
        const Node *floor = row->type == RowType::Grass    ? row->grass->floor
                            : row->type == RowType::Water  ? row->water->floor
                            : row->type == RowType::Road   ? row->road->road
                                                           : row->railRoad->railRoad;
        if (floor->model) heroPlane_ += floorTop(floor->model);
    } else {
        heroPlane_ += real(0.375);
    }

    const uint64_t t0 = profileClock ? profileClock() : 0;
    drawNode(renderer, root, 0);
    uint64_t t1 = profileClock ? profileClock() : 0;
    profile.traverse = int64_t(t1 - t0); // in depth mode this includes the drawing

    if (depthBuffer) {
        drawShadows(renderer);
    } else {
        // painter's order: nothing stands below a floor, and the shadows lie on the floors, so the objects drawn after
        // them cover both
        if (shadowMode != ShadowMode::Off) renderer.clearShadowMask(); // lily pads mark it while the floors draw
        drawFarToNear(renderer, floors_, profile.floors);
        t1 = profileClock ? profileClock() : 0;
        drawShadows(renderer);
        const uint64_t t2 = profileClock ? profileClock() : 0;
        profile.shadows = int64_t(t2 - t1);
        // O16: the log the hero rides is drawn just before the hero, whatever their centres say. The camera looks
        // down from -x, so a hero more than ~1.4 units towards +x of the log's centre sorted in front of it and the
        // log covered all but the hero's head; the logs are up to 3.9 units long, so that is a third of the ride.
        if (ridingEnd_ > ridingBegin_ && heroEnd_ > heroBegin_) {
            rideFrames++;
            int64_t heroFar = objects_[heroBegin_].far, logFar = objects_[ridingBegin_].far;
            for (size_t i = heroBegin_ + 1; i < heroEnd_; i++)
                if (objects_[i].far < heroFar) heroFar = objects_[i].far;
            for (size_t i = ridingBegin_ + 1; i < ridingEnd_; i++)
                if (objects_[i].far < logFar) logFar = objects_[i].far;
            if (logFar <= heroFar) {
                ridePushed++;
                const int64_t push = heroFar - logFar + 1; // farther by the smallest step there is: drawn first
                if (rideFix)
                    for (size_t i = ridingBegin_; i < ridingEnd_; i++) objects_[i].far += push;
            }
        }
        drawFarToNear(renderer, objects_, profile.objects);
    }

    const uint64_t t3 = profileClock ? profileClock() : 0;
    measureFraming(game, height);
    if (profileClock) profile.framing = int64_t(profileClock() - t3);
}

void SceneRenderer::measureFraming(Game &game, int height)
{
    const Mat4 vp = projection * view;
    const Mat4 &wm = game.world()->world; // rows and the hero live in world space
    auto ndc = [&](real x, real y, real z) { return vp.transformPoint(wm.transformPoint({x, y, z})); };
    auto onScreen = [](const Vec3 &n) { return rabs(n.x) <= real(1) && rabs(n.y) <= real(1); };

    Player &hero = game.hero();
    const Vec3 p = hero.position();
    const real row = jsRound(p.z);
    framing = FramingInfo();
    for (int k = 1; k <= 40 && onScreen(ndc(0, 0, row + real(k))); k++) framing.rowsAhead = k;
    for (int k = 1; k <= 40 && onScreen(ndc(0, 0, row - real(k))); k++) framing.rowsBehind = k;
    auto lane = [&](real z) { return onScreen(ndc(real(-4.5), 0, z)) && onScreen(ndc(real(4.5), 0, z)); };
    framing.laneAtHero = lane(row);
    framing.laneAtTop = lane(row + real(framing.rowsAhead));

    const Vec3 feet = ndc(p.x, p.y, p.z);
    framing.heroScreenY = (real(1) - feet.y) / real(2);
    if (hero.node && hero.node->model) {
        const real tall = hero.node->model->aabbMax.y - hero.node->model->aabbMin.y;
        const Vec3 head = ndc(p.x, p.y + tall, p.z);
        framing.heroHeightPx = int(rabs(head.y - feet.y) * real(height) / real(2) + real(0.5));
    }
}

} // namespace cr
