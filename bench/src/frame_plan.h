#pragma once

#include "bench_mode.h"

#include <cstddef>

namespace bench {

// Wheel units per scrolled frame. The engine scrolls 40 px per unit, so 4 px a frame.
inline constexpr float kScrollWheel = -0.1f;

// What one driven tick changes. Everything not named here stays as it was.
struct FramePlan {
    // One-change: the scene's one live value takes its next value.
    bool change_value = false;
    // Hover: move the pointer onto hover target `hover_target`.
    bool move_pointer = false;
    std::size_t hover_target = 0;
    // Scroll: one wheel event of this many units at the wheel point. 0 is none.
    float wheel = 0.0f;
    // Churn: the scene's structural edit for `step`.
    bool churn = false;
    int step = 0;
};

// The plan of mode step `step` (0 on the first driven tick). Hover walks the targets in order, so two frames in a row
// never land on the same one while there are two or more; with none it does not move the pointer.
[[nodiscard]] FramePlan plan_frame(BenchMode mode, int step, std::size_t hover_targets);

}
