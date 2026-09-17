// Minimal three.js-like scene graph: Object3D with position / Euler XYZ rotation / scale, children,
// visibility and an optional model. Nodes come from a pool so regenerating rows never allocates.
#pragma once

#include <deque>
#include <vector>

#include "engine/math.h"

namespace cr {

struct Model; // game/models.h

enum class Shape : unsigned char { None, Box, Plane };

struct Node {
    Vec3 position;
    Vec3 rotation; // Euler XYZ, radians
    Vec3 scale{1, 1, 1};
    bool visible = true;
    const Model *model = nullptr;
    // procedural geometry of particles and foam (BoxGeometry / PlaneGeometry with a flat colour)
    Shape shape = Shape::None;
    Vec3 shapeSize{1, 1, 1};
    Vec3 shapeColor{1, 1, 1};
    bool castShadow = false;
    bool receiveShadow = false;

    Node *parent = nullptr;
    std::vector<Node *> children;
    Mat4 world = Mat4::identity();

    void add(Node *child);
    void remove(Node *child);
    Mat4 localMatrix() const { return composeEuler(position, rotation, scale); }
};

class NodePool {
public:
    Node *alloc();
    // detaches and frees the node and its whole subtree
    void release(Node *node);
    size_t live() const { return nodes_.size() - free_.size(); }

private:
    std::deque<Node> nodes_;
    std::vector<Node *> free_;
};

// Recomputes world matrices of the subtree (THREE.Object3D.updateMatrixWorld).
void updateWorld(Node *root, const Mat4 &parentWorld = Mat4::identity());

// World-space AABB of a subtree's models (THREE.Box3.setFromObject), updating world matrices on the way.
bool worldBounds(Node *root, Vec3 &outMin, Vec3 &outMax);

} // namespace cr
