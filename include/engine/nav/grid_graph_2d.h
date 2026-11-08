#pragma once

#include <engine/ecs/grid_query.h>
#include <engine/nav/nav_types.h>

#include <glm/vec2.hpp>

#include <array>
#include <concepts>
#include <cstdint>
#include <optional>
#include <span>

namespace engine::nav {

// 2D discrete grid graph adapter for pathfinding over tilemaps or arbitrary 2D cell grids.
class GridGraph2D {
public:
    GridGraph2D(
            CellPredicate<glm::ivec2> is_walkable,
            bool diagonals = true,
            bool cut_corners = false)
        : is_walkable_(is_walkable), diagonals_(diagonals), cut_corners_(cut_corners) {}

    void set_bounds(glm::ivec2 min_cell, glm::ivec2 max_cell) noexcept {
        min_cell_ = min_cell;
        max_cell_ = max_cell;
    }

    void clear_bounds() noexcept {
        min_cell_.reset();
        max_cell_.reset();
    }

    [[nodiscard]] bool is_in_bounds(glm::ivec2 cell) const noexcept {
        if (min_cell_ && (cell.x < min_cell_->x || cell.y < min_cell_->y)) {
            return false;
        }
        if (max_cell_ && (cell.x > max_cell_->x || cell.y > max_cell_->y)) {
            return false;
        }
        return true;
    }

    [[nodiscard]] bool is_walkable(glm::ivec2 cell) const {
        return is_in_bounds(cell) && is_walkable_(cell);
    }

    [[nodiscard]] float heuristic(glm::ivec2 a, glm::ivec2 b) const noexcept {
        return diagonals_ ? Heuristic::octile_2d(a, b) : Heuristic::manhattan_2d(a, b);
    }

    template <typename Visitor>
    void neighbors(glm::ivec2 node, Visitor&& visitor) const {
        static constexpr std::array<glm::ivec2, 4> kCardinals{
                glm::ivec2{1, 0}, glm::ivec2{-1, 0}, glm::ivec2{0, 1}, glm::ivec2{0, -1}};

        for (const auto& offset : kCardinals) {
            const glm::ivec2 neighbor = node + offset;
            if (is_walkable(neighbor)) {
                visitor(neighbor, 1.0f);
            }
        }

        if (!diagonals_) {
            return;
        }

        static constexpr std::array<glm::ivec2, 4> kDiagonals{
                glm::ivec2{1, 1}, glm::ivec2{1, -1}, glm::ivec2{-1, 1}, glm::ivec2{-1, -1}};
        constexpr float kDiagCost = 1.41421356f;

        for (const auto& offset : kDiagonals) {
            const glm::ivec2 neighbor = node + offset;
            if (!is_walkable(neighbor)) {
                continue;
            }

            if (!cut_corners_) {
                const glm::ivec2 side_x{node.x + offset.x, node.y};
                const glm::ivec2 side_y{node.x, node.y + offset.y};
                if (!is_walkable(side_x) || !is_walkable(side_y)) {
                    continue;
                }
            }

            visitor(neighbor, kDiagCost);
        }
    }

private:
    CellPredicate<glm::ivec2> is_walkable_;
    bool diagonals_ = true;
    bool cut_corners_ = false;
    std::optional<glm::ivec2> min_cell_;
    std::optional<glm::ivec2> max_cell_;
};

} // namespace engine::nav
