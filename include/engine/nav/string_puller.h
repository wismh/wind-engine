#pragma once

#include <engine/ecs/grid_query.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace engine::nav {

// Returns true if a walker can move straight from `from` to `to` across 2D cells.
// Checks every cell crossed by the line, and ensures diagonal steps do not cut across solid corners.
[[nodiscard]] bool segment_clear_2d(
        glm::ivec2 from, glm::ivec2 to, CellPredicate<glm::ivec2> is_walkable);

// Returns true if a walker can move straight from `from` to `to` across 3D cells on the same floor level (from.y == to.y).
// Checks walkability along the X-Z plane without cutting diagonal corners.
[[nodiscard]] bool segment_clear_3d(
        glm::ivec3 from, glm::ivec3 to, CellPredicate<glm::ivec3> is_walkable);

// Generic string pulling / waypoint reduction algorithm.
// Replaces zigzag paths with straight line-of-sight segments.
template <typename Node, typename VisibilityPredicate>
[[nodiscard]] std::vector<Node> smooth_path(
        std::span<const Node> path,
        VisibilityPredicate&& can_see,
        std::size_t max_lookahead = 0) {
    if (path.size() <= 2) {
        return std::vector<Node>(path.begin(), path.end());
    }

    std::vector<Node> smoothed;
    smoothed.reserve(path.size());
    smoothed.push_back(path[0]);

    std::size_t current = 0;
    while (current + 1 < path.size()) {
        const std::size_t limit = (max_lookahead > 0)
                ? std::min(path.size(), current + 1 + max_lookahead)
                : path.size();

        std::size_t furthest = current + 1;
        for (std::size_t candidate = current + 2; candidate < limit; ++candidate) {
            if (can_see(path[current], path[candidate])) {
                furthest = candidate;
            } else {
                break;
            }
        }

        smoothed.push_back(path[furthest]);
        current = furthest;
    }

    return smoothed;
}

// Convenience wrapper to smooth a discrete 2D grid path.
[[nodiscard]] std::vector<glm::ivec2> smooth_path_2d(
        std::span<const glm::ivec2> path,
        CellPredicate<glm::ivec2> is_walkable,
        std::size_t max_lookahead = 0);

// Convenience wrapper to smooth a discrete 3D grid path.
[[nodiscard]] std::vector<glm::ivec3> smooth_path_3d(
        std::span<const glm::ivec3> path,
        CellPredicate<glm::ivec3> is_walkable,
        std::size_t max_lookahead = 0);

} // namespace engine::nav
