#include "broadphase.h"

#include <algorithm>
#include <cmath>

namespace engine {
namespace {

[[nodiscard]] bool aabb_valid(const Aabb& box) {
    const glm::vec3& lo = box.min;
    const glm::vec3& hi = box.max;
    if (!std::isfinite(lo.x) || !std::isfinite(lo.y) || !std::isfinite(lo.z) || !std::isfinite(hi.x) ||
        !std::isfinite(hi.y) || !std::isfinite(hi.z)) {
        return false;
    }
    return lo.x <= hi.x && lo.y <= hi.y && lo.z <= hi.z;
}

[[nodiscard]] Aabb combine_aabb(const Aabb& a, const Aabb& b) {
    Aabb out;
    out.min.x = std::min(a.min.x, b.min.x);
    out.min.y = std::min(a.min.y, b.min.y);
    out.min.z = std::min(a.min.z, b.min.z);
    out.max.x = std::max(a.max.x, b.max.x);
    out.max.y = std::max(a.max.y, b.max.y);
    out.max.z = std::max(a.max.z, b.max.z);
    return out;
}

[[nodiscard]] float surface_area(const Aabb& box) {
    const glm::vec3 size = box.max - box.min;
    return 2.f * (size.x * size.y + size.y * size.z + size.z * size.x);
}

}

int Broadphase::allocate_node() {
    if (free_ >= 0) {
        const int id = free_;
        free_ = nodes_[static_cast<std::size_t>(id)].next;
        nodes_[static_cast<std::size_t>(id)] = Node{};
        return id;
    }
    nodes_.push_back(Node{});
    return static_cast<int>(nodes_.size()) - 1;
}

float Broadphase::leaf_cost(const Aabb& leaf_box, int child, float inheritance) const {
    const Node& node = nodes_[static_cast<std::size_t>(child)];
    const float grown = surface_area(combine_aabb(leaf_box, node.box));
    if (node.child1 < 0) {
        return grown + inheritance;
    }
    return grown - surface_area(node.box) + inheritance;
}

void Broadphase::insert_leaf(int leaf) {
    if (root_ < 0) {
        root_ = leaf;
        nodes_[static_cast<std::size_t>(leaf)].parent = -1;
        return;
    }

    const Aabb leaf_box = nodes_[static_cast<std::size_t>(leaf)].box;
    int index = root_;
    while (nodes_[static_cast<std::size_t>(index)].child1 >= 0) {
        const int child1 = nodes_[static_cast<std::size_t>(index)].child1;
        const int child2 = nodes_[static_cast<std::size_t>(index)].child2;
        const float area_here = surface_area(nodes_[static_cast<std::size_t>(index)].box);
        const float combined_area = surface_area(combine_aabb(nodes_[static_cast<std::size_t>(index)].box, leaf_box));
        const float cost = 2.f * combined_area;
        const float inheritance = 2.f * (combined_area - area_here);
        const float cost1 = leaf_cost(leaf_box, child1, inheritance);
        const float cost2 = leaf_cost(leaf_box, child2, inheritance);
        if (cost < cost1 && cost < cost2) {
            break;
        }
        // Equal cost keeps child1, the lower child slot.
        index = cost2 < cost1 ? child2 : child1;
    }

    const int sibling = index;
    const int old_parent = nodes_[static_cast<std::size_t>(sibling)].parent;
    const int parent = allocate_node();
    nodes_[static_cast<std::size_t>(parent)].parent = old_parent;
    const Aabb parent_box = combine_aabb(leaf_box, nodes_[static_cast<std::size_t>(sibling)].box);
    nodes_[static_cast<std::size_t>(parent)].box = parent_box;
    nodes_[static_cast<std::size_t>(parent)].child1 = sibling;
    nodes_[static_cast<std::size_t>(parent)].child2 = leaf;
    nodes_[static_cast<std::size_t>(sibling)].parent = parent;
    nodes_[static_cast<std::size_t>(leaf)].parent = parent;
    if (old_parent >= 0) {
        Node& previous = nodes_[static_cast<std::size_t>(old_parent)];
        if (previous.child1 == sibling) {
            previous.child1 = parent;
        } else {
            previous.child2 = parent;
        }
    } else {
        root_ = parent;
    }

    int walk = nodes_[static_cast<std::size_t>(leaf)].parent;
    while (walk >= 0) {
        Node& node = nodes_[static_cast<std::size_t>(walk)];
        node.box = combine_aabb(nodes_[static_cast<std::size_t>(node.child1)].box,
                                nodes_[static_cast<std::size_t>(node.child2)].box);
        walk = node.parent;
    }
}

void Broadphase::rebuild(std::span<const Aabb> boxes) {
    nodes_.clear();
    nodes_.reserve(boxes.size() * 2);
    leaf_of_.assign(boxes.size(), -1);
    root_ = -1;
    free_ = -1;
    for (int i = 0; i < static_cast<int>(boxes.size()); ++i) {
        if (!aabb_valid(boxes[static_cast<std::size_t>(i)])) {
            continue;
        }
        const int leaf = allocate_node();
        nodes_[static_cast<std::size_t>(leaf)].box = boxes[static_cast<std::size_t>(i)];
        nodes_[static_cast<std::size_t>(leaf)].leaf = i;
        leaf_of_[static_cast<std::size_t>(i)] = leaf;
        insert_leaf(leaf);
    }
}

void Broadphase::query_pairs(std::vector<std::pair<int, int>>& out) const {
    out.clear();
    if (root_ < 0) {
        return;
    }
    std::vector<int> stack;
    stack.reserve(32);
    for (int i = 0; i < static_cast<int>(leaf_of_.size()); ++i) {
        const int leaf = leaf_of_[static_cast<std::size_t>(i)];
        if (leaf < 0) {
            continue;
        }
        const Aabb box = nodes_[static_cast<std::size_t>(leaf)].box;
        stack.clear();
        stack.push_back(root_);
        while (!stack.empty()) {
            const int node_index = stack.back();
            stack.pop_back();
            const Node& node = nodes_[static_cast<std::size_t>(node_index)];
            if (!aabb_overlaps(box, node.box)) {
                continue;
            }
            if (node.child1 < 0) {
                if (node.leaf > i) {
                    out.emplace_back(i, node.leaf);
                }
                continue;
            }
            // child1 is pushed last so it is visited first.
            stack.push_back(node.child2);
            stack.push_back(node.child1);
        }
    }
}

}
