#include "scene_render.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/assets.h"
#include "engine/log.h"
#include "game/settings.h"

namespace cr {

using namespace settings;

// Shadowed top faces keep only the ambient term: ((1.8/PI) / ((1.8 + N.L)/PI))^(1/2.2) with N = up
static float shadowFactorForTopFaces()
{
    Vec3 l = normalize({lightX, lightY, lightZ});
    return std::pow(1.8f / (1.8f + l.y), 1.0f / 2.2f);
}

bool SceneRenderer::init(Renderer &renderer, ModelLibrary &models, const Manifest &manifest, const std::string &dataDir)
{
    std::map<const GpuTexture *, TextureData> obstacleTextures;
    for (const auto &entry : manifest.models) {
        const std::string &meshName = entry.second.mesh, &texName = entry.second.texture;
        if (!meshes_.count(meshName)) {
            MeshData md;
            if (!loadMesh(dataDir + "meshes/" + meshName + ".mesh", md)) {
                logf("scene: cannot load mesh %s", meshName.c_str());
                return false;
            }
            meshes_[meshName] = renderer.uploadMesh(md);
            meshData_[&meshes_[meshName]] = std::move(md);
        }
        if (!textures_.count(texName)) {
            TextureData td;
            if (!loadTexture(dataDir + "textures/" + texName + ".tex", td)) {
                logf("scene: cannot load texture %s", texName.c_str());
                return false;
            }
            textures_[texName] = renderer.uploadTexture(td);
            if (entry.first.compare(0, 4, "tree") == 0 || entry.first.compare(0, 7, "boulder") == 0)
                obstacleTextures[&textures_[texName]] = std::move(td);
        }
        if (Model *m = models.find(entry.first)) {
            m->mesh = &meshes_[meshName];
            m->texture = &textures_[texName];
        }
    }
    logf("scene: %zu meshes, %zu textures uploaded", meshes_.size(), textures_.size());
    buildAtlas(renderer, obstacleTextures);
    return true;
}

void SceneRenderer::buildAtlas(Renderer &renderer, const std::map<const GpuTexture *, TextureData> &textures)
{
    if (textures.empty()) return;
    // shelf packing, tallest first, rows at most 2048 px wide (the GLES2 guaranteed texture size); the meshes' UVs
    // stay inside [0.03, 0.97] of their texture, so NEAREST sampling never reaches a neighbour
    const int maxSize = 2048;
    std::vector<std::pair<const GpuTexture *, const TextureData *>> order;
    for (const auto &kv : textures) order.push_back({kv.first, &kv.second});
    std::sort(order.begin(), order.end(),
              [](const std::pair<const GpuTexture *, const TextureData *> &a,
                 const std::pair<const GpuTexture *, const TextureData *> &b) { return a.second->height > b.second->height; });
    std::map<const GpuTexture *, std::pair<int, int>> place;
    int x = 0, y = 0, shelf = 0, width = 0;
    for (const auto &o : order) {
        if (x + o.second->width > maxSize) {
            y += shelf;
            x = 0;
            shelf = 0;
        }
        place[o.first] = {x, y};
        x += o.second->width;
        shelf = std::max(shelf, o.second->height);
        width = std::max(width, x);
    }
    const int height = y + shelf;
    if (width > maxSize || height > maxSize) {
        logf("scene: obstacle atlas %dx%d too large, batches keep one draw per texture", width, height);
        return;
    }
    TextureData atlas;
    atlas.width = width;
    atlas.height = height;
    atlas.channels = 4;
    atlas.pixels.assign(size_t(width) * size_t(height) * 4, 255);
    for (const auto &o : order) {
        const TextureData &t = *o.second;
        const std::pair<int, int> p = place[o.first];
        for (int ty = 0; ty < t.height; ty++)
            for (int tx = 0; tx < t.width; tx++) {
                const uint8_t *s = &t.pixels[(size_t(ty) * size_t(t.width) + size_t(tx)) * size_t(t.channels)];
                uint8_t *d = &atlas.pixels[(size_t(p.second + ty) * size_t(width) + size_t(p.first + tx)) * 4];
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = t.channels == 4 ? s[3] : 255;
            }
        atlasRects_[o.first] = AtlasRect{float(p.first), float(p.second), float(t.width), float(t.height)};
    }
    atlas_ = renderer.uploadTexture(atlas);
    atlasW_ = width;
    atlasH_ = height;
    logf("scene: obstacle atlas %dx%d from %zu textures", width, height, order.size());
}

// height of the surface shadows fall on, in model space: the top of the floor box, except the
// railroad whose ground is at 0.25 under ties (0.375) and rails (0.5)
static float floorTop(const Model *m)
{
    return m->name == "railroad" ? 0.25f : m->aabbMax.y;
}

// static obstacles on grass rows (counted apart in drawsByKind, batched per row)
static bool isStaticObstacle(const Model *m)
{
    return m->name.compare(0, 4, "tree") == 0 || m->name.compare(0, 7, "boulder") == 0;
}

static float maxScale(const Mat4 &m)
{
    float sx = m.e[0] * m.e[0] + m.e[1] * m.e[1] + m.e[2] * m.e[2];
    float sy = m.e[4] * m.e[4] + m.e[5] * m.e[5] + m.e[6] * m.e[6];
    float sz = m.e[8] * m.e[8] + m.e[9] * m.e[9] + m.e[10] * m.e[10];
    return std::sqrt(std::max(sx, std::max(sy, sz)));
}

void SceneRenderer::drawNode(Renderer &renderer, Node *node, const Mat4 &viewProj, float planeY)
{
    if (!node->visible) return;

    const Model *model = node->model;
    const bool wasInHero = inHero_;
    if (node == hero_) {
        planeY = heroPlane_;
        inHero_ = true;
    }
    if ((model && model->mesh) || node->shape != Shape::None) {
        Vec3 mn = model && model->mesh ? model->aabbMin : Vec3{-0.5f, -0.5f, -0.5f};
        Vec3 mx = model && model->mesh ? model->aabbMax : Vec3{0.5f, 0.5f, 0.5f};
        Mat4 world = node->world;
        if (node->shape != Shape::None) world = world * composeEuler({0, 0, 0}, {0, 0, 0}, node->shapeSize);
        Vec3 center = world.transformPoint((mn + mx) * 0.5f);
        float radius = length(mx - mn) * 0.5f * maxScale(world);
        Vec3 clip = viewProj.transformPoint(center);
        // shadows reach ~1.4 units beyond their caster, so casters are kept a little longer
        float margin = node->castShadow ? 2.5f : 1.5f;
        bool outside = std::fabs(clip.x) > 1 + radius * std::fabs(projection.e[0]) * margin ||
                       std::fabs(clip.y) > 1 + radius * std::fabs(projection.e[5]) * margin;
        if (outside) {
            culled++;
        } else if (model && model->mesh) {
            renderer.drawLambert(*model->mesh, *model->texture, world);
            if (inHero_) drawsByKind[KindHero]++;
            else if (model->receiveShadow && !model->castShadow) drawsByKind[KindFloor]++;
            else if (isStaticObstacle(model)) drawsByKind[KindObstacle]++;
            else drawsByKind[KindEntity]++;
            // Simple: the caster's bounding box instead of its mesh (6 lit triangles at most)
            if (node->castShadow && shadowMode == ShadowMode::Full)
                casters_.push_back({model->mesh, world, planeY});
            else if (node->castShadow && shadowMode == ShadowMode::Simple)
                casters_.push_back({&renderer.unitBox, world * composeEuler((mn + mx) * 0.5f, {0, 0, 0}, mx - mn), planeY});
        } else {
            bool plane = node->shape == Shape::Plane;
            renderer.drawFlat(plane ? renderer.unitPlane : renderer.unitBox, node->shapeColor, world, plane);
            drawsByKind[KindShape]++;
        }
        // floors (receive, never cast) define the plane their entities' shadows fall on
        if (model && model->receiveShadow && !model->castShadow) planeY = node->world.e[13] + floorTop(model);
    }
    if (batchStatic && ((model && model->mesh && drawRowBatch(renderer, node, viewProj, planeY)) ||
                        drawShapeGroup(renderer, node, viewProj))) {
        inHero_ = wasInHero;
        return;
    }
    for (Node *c : node->children) drawNode(renderer, c, viewProj, planeY);
    inHero_ = wasInHero;
}

// 6.5b: GrassRow parents its trees and boulders to the floor node and only changes them in generate(). One draw
// call each (plus one shadow pass each) was ~65 + ~65 calls per frame on the R36S's CPU; each row now keeps them
// pre-transformed into one mesh per texture and one shadow mesh, rebuilt whenever the children change.
static uint64_t rowSignature(const Node *floor)
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) {
        for (int i = 0; i < 8; i++) {
            h ^= (v >> (i * 8)) & 0xff;
            h *= 1099511628211ull;
        }
    };
    auto mixd = [&mix](double d) {
        uint64_t v;
        std::memcpy(&v, &d, sizeof v);
        mix(v);
    };
    mix(uint64_t(floor->children.size()));
    for (const Node *c : floor->children) {
        mix(uint64_t(uintptr_t(c->model)));
        mix((c->visible ? 1u : 0u) | (c->castShadow ? 2u : 0u));
        mixd(c->position.x), mixd(c->position.y), mixd(c->position.z);
        mixd(c->rotation.x), mixd(c->rotation.y), mixd(c->rotation.z);
        mixd(c->scale.x), mixd(c->scale.y), mixd(c->scale.z);
    }
    return h;
}

// rect: where the mesh's texture sits in the obstacle atlas (UVs remapped), or null for its own texture
static void appendTransformed(MeshData &dst, const MeshData &src, const Mat4 &local, const Mat4 &normal,
                              const float *rect = nullptr, int atlasW = 1, int atlasH = 1)
{
    const uint16_t base = uint16_t(dst.vertexCount());
    for (int i = 0; i < src.vertexCount(); i++) {
        const float *v = &src.vertices[size_t(i) * 8];
        const Vec3 p = local.transformPoint({v[0], v[1], v[2]});
        const Vec3 n = normal.transformDirection({v[3], v[4], v[5]});
        const float u = rect ? (rect[0] + v[6] * rect[2]) / float(atlasW) : v[6];
        const float t = rect ? (rect[1] + v[7] * rect[3]) / float(atlasH) : v[7];
        const float out[8] = {float(p.x), float(p.y), float(p.z), float(n.x), float(n.y), float(n.z), u, t};
        dst.vertices.insert(dst.vertices.end(), out, out + 8);
    }
    for (uint16_t index : src.indices) dst.indices.push_back(uint16_t(base + index));
}

void SceneRenderer::buildRowBatch(Renderer &renderer, const Node *floor, RowBatch &b)
{
    for (RowBatch::Group &g : b.groups) renderer.releaseMesh(g.mesh);
    b.groups.clear();
    renderer.releaseMesh(b.shadow);
    b.castsShadow = false;
    b.count = 0;
    std::map<const GpuTexture *, MeshData> byTexture;
    MeshData casters;
    for (const Node *c : floor->children) {
        if (!c->visible) continue;
        auto it = meshData_.find(c->model->mesh);
        if (it == meshData_.end()) continue;
        const Mat4 local = c->localMatrix();
        const Mat4 normal = normalMatrix(local);
        auto rect = atlasRects_.find(c->model->texture);
        if (rect != atlasRects_.end()) {
            const float r[4] = {rect->second.x, rect->second.y, rect->second.w, rect->second.h};
            appendTransformed(byTexture[&atlas_], it->second, local, normal, r, atlasW_, atlasH_);
        } else {
            appendTransformed(byTexture[c->model->texture], it->second, local, normal);
        }
        if (c->castShadow) {
            appendTransformed(casters, it->second, local, normal);
            b.castsShadow = true;
        }
        b.count++;
    }
    bool first = true;
    for (auto &kv : byTexture) {
        RowBatch::Group g;
        g.texture = kv.first;
        g.mesh = renderer.uploadMesh(kv.second);
        b.groups.push_back(g);
        for (int i = 0; i < kv.second.vertexCount(); i++) {
            const float *v = &kv.second.vertices[size_t(i) * 8];
            const Vec3 p{v[0], v[1], v[2]};
            if (first) {
                b.min = b.max = p;
                first = false;
            }
            b.min = {std::min(b.min.x, p.x), std::min(b.min.y, p.y), std::min(b.min.z, p.z)};
            b.max = {std::max(b.max.x, p.x), std::max(b.max.y, p.y), std::max(b.max.z, p.z)};
        }
    }
    if (b.castsShadow) b.shadow = renderer.uploadMesh(casters);
}

bool SceneRenderer::drawRowBatch(Renderer &renderer, Node *floor, const Mat4 &viewProj, float planeY)
{
    if (floor->model->name.compare(0, 5, "grass") != 0 || floor->children.empty()) return false;
    for (const Node *c : floor->children)
        if (!c->model || !c->model->mesh || !isStaticObstacle(c->model) || !c->children.empty() ||
            c->shape != Shape::None)
            return false;
    RowBatch &b = rowBatches_[floor];
    const uint64_t signature = rowSignature(floor);
    if (signature != b.signature) {
        buildRowBatch(renderer, floor, b);
        b.signature = signature;
    }
    if (b.count == 0) return true;
    const Mat4 &world = floor->world;
    const Vec3 center = world.transformPoint((b.min + b.max) * 0.5);
    const float radius = float(length(b.max - b.min) * 0.5) * maxScale(world);
    const Vec3 clip = viewProj.transformPoint(center);
    const float margin = b.castsShadow ? 2.5f : 1.5f;
    const bool outside = std::fabs(clip.x) > 1 + radius * std::fabs(projection.e[0]) * margin ||
                         std::fabs(clip.y) > 1 + radius * std::fabs(projection.e[5]) * margin;
    if (outside) {
        culled += b.count;
        return true;
    }
    for (const RowBatch::Group &g : b.groups) {
        renderer.drawLambert(g.mesh, *g.texture, world);
        drawsByKind[KindObstacle]++;
    }
    if (b.castsShadow && shadowMode == ShadowMode::Full) casters_.push_back({&b.shadow, world, planeY});
    if (b.castsShadow && shadowMode == ShadowMode::Simple)
        for (const Node *c : floor->children)
            if (c->visible && c->castShadow)
                casters_.push_back({&renderer.unitBox,
                                    world * c->localMatrix() *
                                        composeEuler((c->model->aabbMin + c->model->aabbMax) * 0.5f, {0, 0, 0},
                                                     c->model->aabbMax - c->model->aabbMin),
                                    planeY});
    return true;
}

bool SceneRenderer::drawShapeGroup(Renderer &renderer, Node *group, const Mat4 &viewProj)
{
    if (group->children.size() < 2) return false;
    const Node *first = group->children[0];
    for (const Node *c : group->children) {
        if (c->model || c->shape == Shape::None || !c->children.empty() || c->shape != first->shape) return false;
        if (c->shapeSize.x != first->shapeSize.x || c->shapeSize.y != first->shapeSize.y ||
            c->shapeSize.z != first->shapeSize.z)
            return false;
        if (c->shapeColor.x != first->shapeColor.x || c->shapeColor.y != first->shapeColor.y ||
            c->shapeColor.z != first->shapeColor.z)
            return false;
    }
    const bool plane = first->shape == Shape::Plane;
    const MeshData &unit = plane ? renderer.unitPlaneData : renderer.unitBoxData;
    shapeScratch_.vertices.clear();
    shapeScratch_.indices.clear();
    int drawn = 0;
    for (const Node *c : group->children) {
        if (!c->visible) continue;
        // as drawNode does for one shape: its world * the geometry's size
        const Mat4 local = c->localMatrix() * composeEuler({0, 0, 0}, {0, 0, 0}, c->shapeSize);
        appendTransformed(shapeScratch_, unit, local, normalMatrix(local));
        drawn++;
    }
    if (drawn == 0) return true;

    Vec3 mn{shapeScratch_.vertices[0], shapeScratch_.vertices[1], shapeScratch_.vertices[2]}, mx = mn;
    for (size_t i = 0; i < shapeScratch_.vertices.size(); i += 8) {
        const float *v = &shapeScratch_.vertices[i];
        mn = {std::min(mn.x, double(v[0])), std::min(mn.y, double(v[1])), std::min(mn.z, double(v[2]))};
        mx = {std::max(mx.x, double(v[0])), std::max(mx.y, double(v[1])), std::max(mx.z, double(v[2]))};
    }
    const Mat4 &world = group->world;
    const Vec3 center = world.transformPoint((mn + mx) * 0.5);
    const float radius = float(length(mx - mn) * 0.5) * maxScale(world);
    const Vec3 clip = viewProj.transformPoint(center);
    const bool outside = std::fabs(clip.x) > 1 + radius * std::fabs(projection.e[0]) * 1.5f ||
                         std::fabs(clip.y) > 1 + radius * std::fabs(projection.e[5]) * 1.5f;
    if (outside) {
        culled += drawn;
        return true;
    }
    renderer.drawFlatDynamic(shapeScratch_, first->shapeColor, world, plane);
    drawsByKind[KindShape]++;
    return true;
}

void SceneRenderer::render(Renderer &renderer, Game &game, int width, int height, float viewScale)
{
    Node *root = game.sceneRoot();
    updateWorld(root);
    setupCamera(game, width, height, viewScale);
    renderScene(renderer, game, height);
}

void SceneRenderer::setupCamera(Game &game, int width, int height, float viewScale)
{
    Mat4 camWorld = lookAtRotation({-1, 2.8f, -2.9f}, {0, 0, 0}, {0, 1, 0});
    Vec3 cp = game.cameraPosition();
    camWorld.e[12] = cp.x;
    camWorld.e[13] = cp.y;
    camWorld.e[14] = cp.z;
    view = inverseRigid(camWorld);
    float w = float(width) * viewScale, h = float(height) * viewScale;
    projection = orthographic(-w, w, h, -h, cameraNear, cameraFar, cameraZoom);
    if (viewShift != 0) {
        Mat4 shift = Mat4::identity();
        shift.e[13] = -viewShift;
        projection = shift * projection;
    }
}

void SceneRenderer::renderScene(Renderer &renderer, Game &game, int height)
{
    Node *root = game.sceneRoot();

    renderer.setCamera(projection, view);
    renderer.setLightDirection({lightX, lightY, lightZ});
    culled = 0;
    casters_.clear();
    for (int &k : drawsByKind) k = 0;

    // the hero is not a child of a row: its shadow falls on the surface under it
    Player &hero = game.hero();
    hero_ = hero.object;
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
        heroPlane_ += 0.375f;
    }

    drawNode(renderer, root, projection * view, 0);

    shadowCasters = int(casters_.size());
    drawsByKind[KindShadow] = shadowMode != ShadowMode::Off ? shadowCasters : 0;
    if (shadowMode != ShadowMode::Off && !casters_.empty()) {
        renderer.beginShadows(shadowFactorForTopFaces());
        for (const Caster &c : casters_) renderer.drawShadow(*c.mesh, c.world, c.planeY);
        renderer.endShadows();
    }

    measureFraming(game, height);
}

void SceneRenderer::measureFraming(Game &game, int height)
{
    const Mat4 vp = projection * view;
    const Mat4 &wm = game.world()->world; // rows and the hero live in world space
    auto ndc = [&](double x, double y, double z) { return vp.transformPoint(wm.transformPoint({x, y, z})); };
    auto onScreen = [](const Vec3 &n) { return std::fabs(n.x) <= 1 && std::fabs(n.y) <= 1; };

    Player &hero = game.hero();
    const Vec3 p = hero.position();
    const double row = jsRound(p.z);
    framing = FramingInfo();
    for (int k = 1; k <= 40 && onScreen(ndc(0, 0, row + k)); k++) framing.rowsAhead = k;
    for (int k = 1; k <= 40 && onScreen(ndc(0, 0, row - k)); k++) framing.rowsBehind = k;
    auto lane = [&](double z) { return onScreen(ndc(-4.5, 0, z)) && onScreen(ndc(4.5, 0, z)); };
    framing.laneAtHero = lane(row);
    framing.laneAtTop = lane(row + framing.rowsAhead);

    Vec3 feet = ndc(p.x, p.y, p.z);
    framing.heroScreenY = (1 - feet.y) / 2;
    if (hero.node && hero.node->model) {
        double tall = hero.node->model->aabbMax.y - hero.node->model->aabbMin.y;
        Vec3 head = ndc(p.x, p.y + tall, p.z);
        framing.heroHeightPx = int(std::fabs(head.y - feet.y) * height / 2 + 0.5);
    }
}

} // namespace cr
