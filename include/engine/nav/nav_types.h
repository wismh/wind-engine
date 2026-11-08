#pragma once

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace engine::nav {

// The outcome of a pathfinding query.
enum class PathStatus : std::uint8_t {
    NotFound = 0,
    Complete,
    Partial,  // Budget exhausted or goal unreachable; path leads to the closest reachable node.
};

// A calculated path of nodes from start to goal (or partial target).
template <typename Node>
struct NavPath {
    std::vector<Node> nodes;
    float total_cost = 0.0f;
    PathStatus status = PathStatus::NotFound;

    [[nodiscard]] bool empty() const noexcept {
        return nodes.empty();
    }

    [[nodiscard]] bool complete() const noexcept {
        return status == PathStatus::Complete;
    }

    [[nodiscard]] bool partial() const noexcept {
        return status == PathStatus::Partial;
    }
};

// Default search limit before falling back to the best reachable node.
inline constexpr int kDefaultSearchBudget = 3000;

struct NavSearchParams {
    int budget = kDefaultSearchBudget;
    bool allow_partial = true;
    bool include_start = false;
};

// Hash specialization helper for common navigation node types.
template <typename Node>
struct NavNodeHash {
    [[nodiscard]] std::size_t operator()(const Node& node) const noexcept {
        return std::hash<Node>{}(node);
    }
};

template <>
struct NavNodeHash<glm::ivec2> {
    [[nodiscard]] std::size_t operator()(glm::ivec2 v) const noexcept {
        constexpr std::uint64_t kBias = 1ULL << 31;
        const std::uint64_t x = static_cast<std::uint64_t>(static_cast<std::int64_t>(v.x) + kBias);
        const std::uint64_t y = static_cast<std::uint64_t>(static_cast<std::int64_t>(v.y) + kBias);
        return static_cast<std::size_t>(x ^ (y << 32));
    }
};

template <>
struct NavNodeHash<glm::ivec3> {
    [[nodiscard]] std::size_t operator()(glm::ivec3 v) const noexcept {
        constexpr std::int64_t kBias = 1 << 20;
        const auto part = [](int val) { return static_cast<std::uint64_t>(val + kBias) & 0x1FFFFF; };
        return static_cast<std::size_t>(part(v.x) | (part(v.y) << 21) | (part(v.z) << 42));
    }
};

// Standard heuristics for discrete and Euclidean grids.
struct Heuristic {
    [[nodiscard]] static float manhattan_2d(glm::ivec2 a, glm::ivec2 b) noexcept {
        return static_cast<float>(std::abs(a.x - b.x) + std::abs(a.y - b.y));
    }

    [[nodiscard]] static float euclidean_2d(glm::ivec2 a, glm::ivec2 b) noexcept {
        const float dx = static_cast<float>(a.x - b.x);
        const float dy = static_cast<float>(a.y - b.y);
        return std::sqrt(dx * dx + dy * dy);
    }

    [[nodiscard]] static float octile_2d(glm::ivec2 a, glm::ivec2 b) noexcept {
        const float dx = static_cast<float>(std::abs(a.x - b.x));
        const float dy = static_cast<float>(std::abs(a.y - b.y));
        constexpr float kDiag = 1.41421356f - 1.0f;
        return (dx > dy) ? (dx + kDiag * dy) : (dy + kDiag * dx);
    }

    [[nodiscard]] static float manhattan_3d(glm::ivec3 a, glm::ivec3 b) noexcept {
        return static_cast<float>(std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z));
    }

    [[nodiscard]] static float euclidean_3d(glm::ivec3 a, glm::ivec3 b) noexcept {
        const float dx = static_cast<float>(a.x - b.x);
        const float dy = static_cast<float>(a.y - b.y);
        const float dz = static_cast<float>(a.z - b.z);
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
};

} // namespace engine::nav
