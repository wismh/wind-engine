#pragma once

#include <glm/vec3.hpp>

#include <cstddef>
#include <optional>
#include <vector>

namespace engine::nav {

// Navigation agent component for moving entities along waypoints or following targets.
// Outputs `desired_velocity` without directly modifying Transform, leaving physical movement,
// collision sliding, and crowd avoidance to dedicated controllers.
struct NavAgent {
    std::optional<glm::vec3> target;
    std::vector<glm::vec3> waypoints;
    std::size_t current_waypoint_index = 0;

    float desired_speed = 3.0f;
    glm::vec3 desired_velocity{0.0f};
    float waypoint_reach_radius = 0.5f;

    bool repath_requested = false;
    float repath_interval = 0.5f;
    float time_since_repath = 0.0f;
    bool reached_target = false;

    void set_target(glm::vec3 new_target) {
        target = new_target;
        repath_requested = true;
        reached_target = false;
    }

    void set_waypoints(std::vector<glm::vec3> new_waypoints) {
        waypoints = std::move(new_waypoints);
        current_waypoint_index = 0;
        reached_target = waypoints.empty();
        if (reached_target) {
            desired_velocity = glm::vec3{0.0f};
        }
    }

    void stop() {
        target.reset();
        waypoints.clear();
        current_waypoint_index = 0;
        desired_velocity = glm::vec3{0.0f};
        reached_target = true;
        repath_requested = false;
    }

    [[nodiscard]] bool has_target() const noexcept {
        return target.has_value();
    }

    [[nodiscard]] bool has_waypoints() const noexcept {
        return !waypoints.empty() && current_waypoint_index < waypoints.size();
    }
};

} // namespace engine::nav
