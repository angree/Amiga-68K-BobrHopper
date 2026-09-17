#include "scene.h"

#include <algorithm>

#include "models.h"

namespace cr {

void Node::add(Node *child)
{
    if (child->parent) child->parent->remove(child);
    child->parent = this;
    children.push_back(child);
}

void Node::remove(Node *child)
{
    auto it = std::find(children.begin(), children.end(), child);
    if (it != children.end()) {
        children.erase(it);
        child->parent = nullptr;
    }
}

Node *NodePool::alloc()
{
    Node *n;
    if (!free_.empty()) {
        n = free_.back();
        free_.pop_back();
        *n = Node();
    } else {
        nodes_.emplace_back();
        n = &nodes_.back();
    }
    return n;
}

void NodePool::release(Node *node)
{
    if (!node) return;
    if (node->parent) node->parent->remove(node);
    std::vector<Node *> stack{node};
    while (!stack.empty()) {
        Node *n = stack.back();
        stack.pop_back();
        for (Node *c : n->children) {
            c->parent = nullptr;
            stack.push_back(c);
        }
        n->children.clear();
        n->model = nullptr;
        free_.push_back(n);
    }
}

void updateWorld(Node *root, const Mat4 &parentWorld)
{
#ifdef CR_FIXED
    root->world = mulAffine(parentWorld, root->localMatrix()); // scene matrices are affine
#else
    root->world = parentWorld * root->localMatrix();
#endif
    for (Node *c : root->children) updateWorld(c, root->world);
}

static void expand(Node *n, bool &any, Vec3 &mn, Vec3 &mx)
{
    if (n->model) {
        const Vec3 &a = n->model->aabbMin, &b = n->model->aabbMax;
        for (int c = 0; c < 8; c++) {
            Vec3 p{(c & 1) ? b.x : a.x, (c & 2) ? b.y : a.y, (c & 4) ? b.z : a.z};
            Vec3 w = n->world.transformPoint(p);
            if (!any) {
                mn = mx = w;
                any = true;
            } else {
                mn = {std::min(mn.x, w.x), std::min(mn.y, w.y), std::min(mn.z, w.z)};
                mx = {std::max(mx.x, w.x), std::max(mx.y, w.y), std::max(mx.z, w.z)};
            }
        }
    }
    for (Node *c : n->children) expand(c, any, mn, mx);
}

bool worldBounds(Node *root, Vec3 &outMin, Vec3 &outMax)
{
    Mat4 parentWorld = Mat4::identity();
    std::vector<Node *> chain;
    for (Node *p = root->parent; p; p = p->parent) chain.push_back(p);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) parentWorld = parentWorld * (*it)->localMatrix();
    updateWorld(root, parentWorld);
    bool any = false;
    expand(root, any, outMin, outMax);
    return any;
}

} // namespace cr
