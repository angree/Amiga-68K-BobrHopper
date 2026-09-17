#include "models.h"

#include <cmath>

#include "engine/assets.h"
#include "engine/log.h"

namespace cr {

const Model *ModelGroup::get(const std::string &key) const
{
    for (const auto &k : keys)
        if (k.first == key) return k.second;
    return nullptr;
}

Model *ModelLibrary::find(const std::string &name)
{
    auto it = models.find(name);
    return it == models.end() ? nullptr : &it->second;
}

bool ModelLibrary::load(const Manifest &manifest, const std::string &dataDir)
{
    for (const auto &entry : manifest.models) {
#ifdef CR_FIXED
        // SF2000 data carries flat-colour meshes, one per model, with the AABB bits of the .mesh
        // (tools/bake_flat.py); the plain .mesh stays the fallback for the logic-only host tools
        FlatMeshData md;
        if (!loadFlatMesh(dataDir + "meshes/" + entry.first + ".fmesh", md)) {
            MeshData plain;
            if (!loadMesh(dataDir + "meshes/" + entry.second.mesh + ".mesh", plain)) {
                logf("models: cannot load mesh %s", entry.second.mesh.c_str());
                return false;
            }
            for (int i = 0; i < 3; i++) {
                md.aabbMin[i] = plain.aabbMin[i];
                md.aabbMax[i] = plain.aabbMax[i];
            }
        }
#else
        MeshData md;
        if (!loadMesh(dataDir + "meshes/" + entry.second.mesh + ".mesh", md)) {
            logf("models: cannot load mesh %s", entry.second.mesh.c_str());
            return false;
        }
#endif
        Model m;
        m.name = entry.first;
        // the files store IEEE floats; on the SF2000 realFromFloat turns their bits into 16.16 without float maths
        m.aabbMin = {realFromFloat(md.aabbMin[0]), realFromFloat(md.aabbMin[1]), realFromFloat(md.aabbMin[2])};
        m.aabbMax = {realFromFloat(md.aabbMax[0]), realFromFloat(md.aabbMax[1]), realFromFloat(md.aabbMax[2])};
        models[entry.first] = m;
    }

    auto group = [&](ModelGroup &g, std::initializer_list<std::pair<const char *, const char *>> keys, bool cast,
                     bool receive) {
        for (const auto &k : keys) {
            Model *m = find(k.second);
            if (!m) {
                logf("models: manifest lacks %s", k.second);
                continue;
            }
            m->castShadow = cast;
            m->receiveShadow = receive;
            g.keys.push_back({k.first, m});
        }
    };
    // src/Node/*.ts registration order and shadow flags
    group(lilyPad, {{"0", "lily_pad"}}, true, true);
    group(grass, {{"0", "grass_0"}, {"1", "grass_1"}}, false, true);
    group(road, {{"0", "road_0"}, {"1", "road_1"}}, false, true);
    group(river, {{"0", "river"}}, false, true);
    group(boulder, {{"0", "boulder_0"}, {"1", "boulder_1"}}, true, false);
    group(tree, {{"0", "tree_0"}, {"1", "tree_1"}, {"2", "tree_2"}, {"3", "tree_3"}}, true, false);
    group(car,
          {{"0", "police_car"}, {"1", "blue_car"}, {"2", "blue_truck"}, {"3", "green_car"}, {"4", "orange_car"},
           {"5", "purple_car"}, {"6", "red_truck"}, {"7", "taxi"}},
          true, true);
    group(railroad, {{"0", "railroad"}}, false, true);
    group(train, {{"front", "train_front"}, {"middle", "train_middle"}, {"back", "train_back"}}, true, true);
    // TrainLight registers "0" (inactive) without shadow flags (Generic defaults: undefined -> false)
    group(trainLight,
          {{"0", "train_light_inactive"}, {"active_0", "train_light_active_0"}, {"active_1", "train_light_active_1"}},
          false, false);
    group(log, {{"0", "log_0"}, {"1", "log_1"}, {"2", "log_2"}, {"3", "log_3"}}, true, true);
    group(hero,
          {{"chicken", "chicken"}, {"beaver", "beaver"}, // O14: the beaver is the port's own model (tools/make_beaver.py)
           {"brent", "brent"}, {"avocoder", "avocoder"}, {"bacon", "bacon"},
           {"wheeler", "wheeler"}, {"palmer", "palmer"}, {"juwan", "juwan"}},
          true, true);
    return true;
}

Node *ModelLibrary::getNode(const ModelGroup &group, const std::string &key, NodePool &pool) const
{
    const Model *m = group.get(key);
    Node *n = pool.alloc();
    n->model = m;
    if (m) {
        n->castShadow = m->castShadow;
        n->receiveShadow = m->receiveShadow;
    }
    return n;
}

Node *ModelLibrary::getRandom(const ModelGroup &group, Rng &rng, NodePool &pool) const
{
    // keys[(keys.length * Math.random()) << 0] -- consumes a number even with a single key
    size_t index = size_t(int(real(int(group.keys.size())) * rng.next()));
    if (index >= group.keys.size()) index = group.keys.size() - 1;
    return getNode(group, group.keys[index].first, pool);
}

static int roundedExtent(Node *node, int axis)
{
    Vec3 mn, mx;
    if (!worldBounds(node, mn, mx)) return 0;
    real d = axis == 0 ? mx.x - mn.x : mx.z - mn.z;
    return int(jsRound(d));
}

int roundedWidthX(Node *node) { return roundedExtent(node, 0); }
int roundedWidthZ(Node *node) { return roundedExtent(node, 2); }

Node *ModelLibrary::trainWithSize(real size, NodePool &pool) const
{
    Node *group = pool.alloc();
    Node *front = getNode(train, "front", pool);
    group->add(front);
    // getDepth measures the part on its own (before it is positioned)
    real offset = real(roundedWidthX(front));
    for (int i = 0; real(i) < size; i++) {
        Node *middle = getNode(train, "middle", pool);
        middle->position.x = offset;
        group->add(middle);
        Node probe;
        probe.model = middle->model;
        offset += real(roundedWidthX(&probe));
    }
    Node *back = getNode(train, "back", pool);
    back->position.x = offset;
    group->add(back);
    return group;
}

} // namespace cr
