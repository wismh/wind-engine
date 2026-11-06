#pragma once

// Private. Deterministic dynamic AABB tree. No World. Not installed.

#include <glm/vec3.hpp>

#include <span>
#include <utility>
#include <vector>

namespace engine {

// Closed bounds. A shared face overlaps. A gap of one ulp does not. No inflation.
struct Aabb {
    glm::vec3 min{0.f};
    glm::vec3 max{0.f};
};

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
// The same span produces the same pair list. Callers sort.
class Broadphase {
public:
    void rebuild(std::span<const Aabb> boxes);
    void query_pairs(std::vector<std::pair<int, int>>& out) const;

private:
    struct Node {
        Aabb box{};
        int parent{-1};
        int child1{-1};
        int child2{-1};
        int leaf{-1};
        int next{-1};
    };

    [[nodiscard]] int allocate_node();
    void insert_leaf(int leaf);
    [[nodiscard]] float leaf_cost(const Aabb& leaf_box, int child, float inheritance) const;

    std::vector<Node> nodes_{};
    std::vector<int> leaf_of_{};
    int root_{-1};
    int free_{-1};
};

}
