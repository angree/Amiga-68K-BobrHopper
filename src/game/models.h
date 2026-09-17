// Model registry: src/ModelLoader.ts + src/Node/*.ts. Models are the units the game code asks for
// ("tree" key "2"); render handles are optional so the logic runs headless with AABBs only.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "engine/math.h"
#include "game/rng.h"
#include "game/scene.h"

namespace cr {

struct GpuMesh;
struct GpuTexture;
struct Manifest;

struct Model {
    std::string name;
    Vec3 aabbMin, aabbMax;
    bool castShadow = false, receiveShadow = false;
    const GpuMesh *mesh = nullptr;       // null when headless
    const GpuTexture *texture = nullptr; // null when headless
};

// One src/Node/*.ts class: keys in JavaScript Object.keys order (integer-like keys ascending, then
// insertion order), because Generic.getRandom indexes that list.
struct ModelGroup {
    std::vector<std::pair<std::string, const Model *>> keys;
    const Model *get(const std::string &key) const;
};

class ModelLibrary {
public:
    // Loads AABBs (and nothing else) for every model in the manifest.
    bool load(const Manifest &manifest, const std::string &dataDir);
    Model *find(const std::string &name);

    ModelGroup lilyPad, grass, road, river, boulder, tree, car, railroad, train, trainLight, log, hero;

    // Generic.getRandom(): keys[(keys.length * Math.random()) << 0].clone()
    Node *getRandom(const ModelGroup &group, Rng &rng, NodePool &pool) const;
    // Generic.getNode(key).clone()
    Node *getNode(const ModelGroup &group, const std::string &key, NodePool &pool) const;
    // Train.withSize(size): front, ceil(size) middles, back, offset by Math.round of their x extents
    Node *trainWithSize(real size, NodePool &pool) const;

    std::map<std::string, Model> models;
};

// Math.round(box.max.x - box.min.x) / (.z) on a subtree, as Row/*.ts getWidth does.
int roundedWidthX(Node *node);
int roundedWidthZ(Node *node);

// jsRound (JavaScript Math.round) lives in engine/real.h

} // namespace cr
