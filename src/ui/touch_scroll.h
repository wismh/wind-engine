#pragma once

// docs/tech/features/UI Input.md, Touch scrolling

#include <glm/vec2.hpp>

namespace engine::ui {

    // Layout units a finger moves before a press over a scroller becomes a swipe.
    inline constexpr float kTouchSlop = 8.0f;
    // Layout units per second: a release slower than this does not fling, a fling slower than kFlingStopSpeed stops.
    inline constexpr float kFlingMinSpeed = 60.0f;
    inline constexpr float kFlingStopSpeed = 15.0f;
    inline constexpr float kFlingMaxSpeed = 6000.0f;

    [[nodiscard]] bool past_touch_slop(glm::vec2 moved_layout) noexcept;

    // The smoothed velocity after a frame of `dt` seconds in which the scroll moved by `moved`.
    [[nodiscard]] glm::vec2 track_scroll_velocity(glm::vec2 velocity, glm::vec2 moved, float dt) noexcept;

    // The velocity of a fling `dt` seconds later: exponential friction.
    [[nodiscard]] glm::vec2 decay_fling_velocity(glm::vec2 velocity, float dt) noexcept;

    // `velocity` with its length capped at kFlingMaxSpeed.
    [[nodiscard]] glm::vec2 clamp_fling_velocity(glm::vec2 velocity) noexcept;

} // namespace engine::ui
