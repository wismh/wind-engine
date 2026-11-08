#pragma once

// Private. Deterministic dynamic AABB tree. No World. Not installed.

#include <glm/vec3.hpp>

#include <cmath>
#include <span>
#include <utility>
#include <vector>

namespace engine {

// Closed bounds. A shared face overlaps. A gap of one ulp does not. No inflation.
struct Aabb {
    glm::vec3 min{0.f};
    glm::vec3 max{0.f};
};

[[nodiscard]] inline bool aabb_valid(const Aabb& box) {
    const glm::vec3& lo = box.min;
    const glm::vec3& hi = box.max;
    if (!std::isfinite(lo.x) || !std::isfinite(lo.y) || !std::isfinite(lo.z) || !std::isfinite(hi.x) ||
        !std::isfinite(hi.y) || !std::isfinite(hi.z)) {
        return false;
    }
    return lo.x <= hi.x && lo.y <= hi.y && lo.z <= hi.z;
}

[[nodiscard]] inline bool aabb_overlaps(const Aabb& a, const Aabb& b) {
    if (a.max.x < b.min.x || b.max.x < a.min.x) {
        return false;
    }
    if (a.max.y < b.min.y || b.max.y < a.min.y) {
        return false;
    }
    if (a.max.z < b.min.z || b.max.z < a.min.z) {
        return false;
    }
    return true;
}

// Indices match the span passed to rebuild. An inverted or non-finite box overlaps nothing.
// The same span produces the same pair list. query_sorted_pairs sorts it by index.
class Broadphase {
public:
    void rebuild(std::span<const Aabb> boxes);
    void query_pairs(std::vector<std::pair<int, int>>& out) const;
    void query(const Aabb& box, std::vector<int>& out) const;

    template <typename Callback>
    void query(const Aabb& box, Callback&& callback) const {
        if (root_ < 0 || !aabb_valid(box)) {
            return;
        }
        std::vector<int> stack;
        stack.reserve(32);
        stack.push_back(root_);
        while (!stack.empty()) {
            const int node_index = stack.back();
            stack.pop_back();
            const Node& node = nodes_[static_cast<std::size_t>(node_index)];
            if (!aabb_overlaps(box, node.box)) {
                continue;
            }
            if (node.child1 < 0) {
                callback(node.leaf);
                continue;
            }
            stack.push_back(node.child2);
            stack.push_back(node.child1);
        }
    }

    [[nodiscard]] bool empty() const noexcept {
        return root_ < 0;
    }

    [[nodiscard]] int height() const noexcept {
        return root_ < 0 ? 0 : nodes_[static_cast<std::size_t>(root_)].height;
    }

private:
    struct Node {
        Aabb box{};
        int parent{-1};
        int child1{-1};
        int child2{-1};
        int leaf{-1};
        int next{-1};
        int height{0};
    };

    [[nodiscard]] int allocate_node();
    void insert_leaf(int leaf);
    [[nodiscard]] int balance(int index);
    [[nodiscard]] float leaf_cost(const Aabb& leaf_box, int child, float inheritance) const;

    std::vector<Node> nodes_{};
    std::vector<int> leaf_of_{};
    int root_{-1};
    int free_{-1};
};

// Rebuilds the tree, queries pairs, and sorts them by collider index. An inverted box overlaps nothing.
[[nodiscard]] std::vector<std::pair<int, int>> query_sorted_pairs(std::span<const Aabb> boxes);

}
