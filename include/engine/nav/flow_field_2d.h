#pragma once

#include <engine/ecs/grid_query.h>

#include <glm/vec2.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace engine::nav {

// A 2D vector field directed towards one or more goals, calculated via reverse Dijkstra.
// Ideal for crowds, swarms, and RTS units: all agents querying the field get an O(1) steering direction.
class FlowField2D {
public:
    struct CellData {
        glm::ivec2 next{0};
        glm::vec2 direction{0.0f};
        float cost = 1e9f;
    };

    FlowField2D() = default;

    void init_grid(glm::ivec2 origin, glm::ivec2 span);

    // O(K) reset: resets only cells touched during the last build without reallocating or clearing the whole buffer.
    void reset() noexcept;

    // Builds the flow field spreading outward from `goals` up to `max_range`.
    void build(
            std::span<const glm::ivec2> goals,
            CellPredicate<glm::ivec2> is_walkable,
            float max_range = 100.0f,
            bool diagonals = true);

    [[nodiscard]] bool empty() const noexcept {
        return cells_.empty();
    }

    [[nodiscard]] glm::ivec2 origin() const noexcept {
        return origin_;
    }

    [[nodiscard]] glm::ivec2 span() const noexcept {
        return span_;
    }

    [[nodiscard]] int cell_index(glm::ivec2 cell) const noexcept {
        const int ox = cell.x - origin_.x;
        const int oy = cell.y - origin_.y;
        if (ox < 0 || ox >= span_.x || oy < 0 || oy >= span_.y) {
            return -1;
        }
        return ox + span_.x * oy;
    }

    [[nodiscard]] glm::ivec2 cell_at(int idx) const noexcept {
        return {origin_.x + (idx % span_.x), origin_.y + (idx / span_.x)};
    }

    [[nodiscard]] bool has_flow(glm::ivec2 cell) const noexcept;

    [[nodiscard]] glm::vec2 direction(glm::ivec2 cell) const noexcept;

    [[nodiscard]] std::optional<glm::ivec2> next_cell(glm::ivec2 cell) const noexcept;

    [[nodiscard]] float cost(glm::ivec2 cell) const noexcept;

    // Follows `next` pointers to reach the goal. Does not include `start`.
    [[nodiscard]] std::vector<glm::ivec2> path_to_goal(glm::ivec2 start, std::size_t max_steps = 256) const;

private:
    glm::ivec2 origin_{0};
    glm::ivec2 span_{0};
    std::vector<CellData> cells_;
    std::vector<int> touched_;
};

} // namespace engine::nav
