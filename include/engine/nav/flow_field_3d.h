#pragma once

#include <engine/ecs/grid_query.h>

#include <glm/vec3.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace engine::nav {

// A 3D vector field directed towards one or more goals, calculated via reverse Dijkstra.
// Supports step-ups (jumping up onto blocks) and drops.
class FlowField3D {
public:
    struct CellData {
        glm::ivec3 next{0};
        glm::vec3 direction{0.0f};
        float cost = 1e9f;
    };

    FlowField3D() = default;

    void init_grid(glm::ivec3 origin, glm::ivec3 span);

    // O(K) reset: resets only touched cells without clearing the whole 3D volume.
    void reset() noexcept;

    void build(
            std::span<const glm::ivec3> goals,
            CellPredicate<glm::ivec3> can_stand,
            int max_step_up = 1,
            int max_drop = 3,
            float max_range = 75.0f);

    [[nodiscard]] bool empty() const noexcept {
        return cells_.empty();
    }

    [[nodiscard]] glm::ivec3 origin() const noexcept {
        return origin_;
    }

    [[nodiscard]] glm::ivec3 span() const noexcept {
        return span_;
    }

    [[nodiscard]] int cell_index(glm::ivec3 cell) const noexcept {
        const int ox = cell.x - origin_.x;
        const int oy = cell.y - origin_.y;
        const int oz = cell.z - origin_.z;
        if (ox < 0 || ox >= span_.x || oy < 0 || oy >= span_.y || oz < 0 || oz >= span_.z) {
            return -1;
        }
        return ox + span_.x * (oy + span_.y * oz);
    }

    [[nodiscard]] glm::ivec3 cell_at(int idx) const noexcept {
        const int ox = idx % span_.x;
        const int rem = idx / span_.x;
        const int oy = rem % span_.y;
        const int oz = rem / span_.y;
        return {origin_.x + ox, origin_.y + oy, origin_.z + oz};
    }

    [[nodiscard]] bool has_flow(glm::ivec3 cell) const noexcept;

    [[nodiscard]] glm::vec3 direction(glm::ivec3 cell) const noexcept;

    [[nodiscard]] std::optional<glm::ivec3> next_cell(glm::ivec3 cell) const noexcept;

    [[nodiscard]] float cost(glm::ivec3 cell) const noexcept;

    [[nodiscard]] std::vector<glm::ivec3> path_to_goal(glm::ivec3 start, std::size_t max_steps = 256) const;

private:
    glm::ivec3 origin_{0};
    glm::ivec3 span_{0};
    std::vector<CellData> cells_;
    std::vector<int> touched_;
};

} // namespace engine::nav
