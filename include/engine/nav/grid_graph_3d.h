#pragma once

#include <engine/ecs/grid_query.h>
#include <engine/nav/nav_types.h>

#include <glm/vec3.hpp>

#include <array>
#include <concepts>
#include <optional>

namespace engine::nav {

// 3D stepped grid graph adapter for voxel/block worlds and layered 3D spaces.
// Supports step-ups (jumping/climbing ledges) and controlled drops down.
class GridGraph3D {
public:
    inline static constexpr int kDefaultMaxStepUp = 1;
    inline static constexpr int kDefaultMaxDrop = 3;

    GridGraph3D(
            CellPredicate<glm::ivec3> can_stand,
            int max_step_up = kDefaultMaxStepUp,
            int max_drop = kDefaultMaxDrop,
            bool diagonals = true,
            bool cut_corners = false)
        : can_stand_(can_stand),
          max_step_up_(max_step_up),
          max_drop_(max_drop),
          diagonals_(diagonals),
          cut_corners_(cut_corners) {}

    void set_bounds(glm::ivec3 min_cell, glm::ivec3 max_cell) noexcept {
        min_cell_ = min_cell;
        max_cell_ = max_cell;
    }

    void clear_bounds() noexcept {
        min_cell_.reset();
        max_cell_.reset();
    }

    [[nodiscard]] bool is_in_bounds(glm::ivec3 cell) const noexcept {
        if (min_cell_ && (cell.x < min_cell_->x || cell.y < min_cell_->y || cell.z < min_cell_->z)) {
            return false;
        }
        if (max_cell_ && (cell.x > max_cell_->x || cell.y > max_cell_->y || cell.z > max_cell_->z)) {
            return false;
        }
        return true;
    }

    [[nodiscard]] bool can_stand(glm::ivec3 feet) const {
        return is_in_bounds(feet) && can_stand_(feet);
    }

    [[nodiscard]] float heuristic(glm::ivec3 a, glm::ivec3 b) const noexcept {
        return Heuristic::euclidean_3d(a, b);
    }

    template <typename Visitor>
    void neighbors(glm::ivec3 node, Visitor&& visitor) const {
        static constexpr std::array<glm::ivec2, 4> kCardinals{
                glm::ivec2{1, 0}, glm::ivec2{-1, 0}, glm::ivec2{0, 1}, glm::ivec2{0, -1}};

        for (const auto& offset : kCardinals) {
            probe_step(node, offset.x, offset.y, 1.0f, visitor);
        }

        if (!diagonals_) {
            return;
        }

        static constexpr std::array<glm::ivec2, 4> kDiagonals{
                glm::ivec2{1, 1}, glm::ivec2{1, -1}, glm::ivec2{-1, 1}, glm::ivec2{-1, -1}};
        constexpr float kDiagCost = 1.41421356f;

        for (const auto& offset : kDiagonals) {
            if (!cut_corners_) {
                const glm::ivec3 side_x{node.x + offset.x, node.y, node.z};
                const glm::ivec3 side_z{node.x, node.y, node.z + offset.y};
                if (!can_stand(side_x) || !can_stand(side_z)) {
                    continue;
                }
            }
            probe_step(node, offset.x, offset.y, kDiagCost, visitor);
        }
    }

private:
    template <typename Visitor>
    void probe_step(glm::ivec3 from, int dx, int dz, float base_cost, Visitor&& visitor) const {
        // 1. Level step (dy = 0)
        const glm::ivec3 level{from.x + dx, from.y, from.z + dz};
        if (can_stand(level)) {
            visitor(level, base_cost);
            return;
        }

        // 2. Step up (jumping onto a ledge)
        for (int dy = 1; dy <= max_step_up_; ++dy) {
            const glm::ivec3 up{from.x + dx, from.y + dy, from.z + dz};
            if (can_stand(up)) {
                visitor(up, base_cost + static_cast<float>(dy) * 0.5f);
                return;
            }
        }

        // 3. Drop down
        for (int dy = 1; dy <= max_drop_; ++dy) {
            const glm::ivec3 down{from.x + dx, from.y - dy, from.z + dz};
            if (can_stand(down)) {
                visitor(down, base_cost + static_cast<float>(dy) * 0.2f);
                return;
            }
        }
    }

    CellPredicate<glm::ivec3> can_stand_;
    int max_step_up_ = kDefaultMaxStepUp;
    int max_drop_ = kDefaultMaxDrop;
    bool diagonals_ = true;
    bool cut_corners_ = false;
    std::optional<glm::ivec3> min_cell_;
    std::optional<glm::ivec3> max_cell_;
};

} // namespace engine::nav
