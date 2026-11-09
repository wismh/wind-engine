#pragma once

// Private. Deterministic dynamic AABB tree. No World. Not installed.

#include <glm/vec3.hpp>

#include <array>
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
    // Incremental edit of one index, as if rebuild had been given `box` there: an existing leaf is removed, then an
    // inverted or non-finite box leaves the index empty and any other box is inserted. Queries return the same set;
    // the tree's shape depends on the order of edits.
    void set(int index, const Aabb& box);
    // Leaves the index empty.
    void erase(int index);
    void query_pairs(std::vector<std::pair<int, int>>& out) const;
    void query(const Aabb& box, std::vector<int>& out) const;

    template <typename Callback>
    void query(const Aabb& box, Callback&& callback) const {
        if (root_ < 0 || !aabb_valid(box)) {
            return;
        }
        std::array<int, 64> inline_stack;
        std::vector<int> overflow_stack;
        int top = 0;
        inline_stack[static_cast<std::size_t>(top++)] = root_;
        auto push_node = [&](int index) {
            if (top < static_cast<int>(inline_stack.size())) {
                inline_stack[static_cast<std::size_t>(top++)] = index;
            } else {
                overflow_stack.push_back(index);
            }
        };
        auto pop_node = [&]() {
            if (!overflow_stack.empty()) {
                const int index = overflow_stack.back();
                overflow_stack.pop_back();
                return index;
            }
            return inline_stack[static_cast<std::size_t>(--top)];
        };
        while (top > 0 || !overflow_stack.empty()) {
            const int node_index = pop_node();
            const Node& node = nodes_[static_cast<std::size_t>(node_index)];
            if (!aabb_overlaps(box, node.box)) {
                continue;
            }
            if (node.child1 < 0) {
                callback(node.leaf);
                continue;
            }
            push_node(node.child2);
            push_node(node.child1);
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
    void remove_leaf(int leaf);
    void free_node(int index);
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
