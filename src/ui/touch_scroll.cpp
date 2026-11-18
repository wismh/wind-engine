#include "ui/touch_scroll.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace engine::ui {

    namespace {

        // Seconds over which a new velocity sample replaces the old one.
        constexpr float kVelocitySmoothing = 0.05f;
        // Per second: a fling keeps exp(-kFlingFriction) of its speed after one second.
        constexpr float kFlingFriction = 2.5f;

    } // namespace

    bool past_touch_slop(glm::vec2 moved_layout) noexcept { return glm::length(moved_layout) >= kTouchSlop; }

    glm::vec2 track_scroll_velocity(glm::vec2 velocity, glm::vec2 moved, float dt) noexcept {
        if (dt <= 0.0f) {
            return velocity;
        }
        const float weight = std::min(1.0f, dt / kVelocitySmoothing);
        return velocity + (moved / dt - velocity) * weight;
    }

    glm::vec2 decay_fling_velocity(glm::vec2 velocity, float dt) noexcept {
        if (dt <= 0.0f) {
            return velocity;
        }
        return velocity * std::exp(-kFlingFriction * dt);
    }

    glm::vec2 clamp_fling_velocity(glm::vec2 velocity) noexcept {
        const float speed = glm::length(velocity);
        if (speed <= kFlingMaxSpeed) {
            return velocity;
        }
        return velocity * (kFlingMaxSpeed / speed);
    }

} // namespace engine::ui
