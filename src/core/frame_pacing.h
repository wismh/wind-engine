#pragma once

// docs/tech/features/Windowing.md#frame-pacing

#include <engine/core/window_desc.h>

#include <chrono>
#include <optional>
#include <span>

namespace engine {

// One live window as the frame pacer sees it. `presentable` is false while the window is hidden, minimized, or
// occluded: a swap there may return at once instead of waiting for the display.
struct PacingWindow {
    WindowId id;
    bool presentable = false;
    bool vsync_supported = false;
};

// The one window whose swap waits for vblank. Every other window swaps without waiting, so a tick waits once,
// however many windows the process has. kPrimaryWindow when it can pace, else the lowest id that can. nullopt
// when no window can: the frame limiter paces the tick instead.
[[nodiscard]] std::optional<WindowId> choose_vsync_window(std::span<const PacingWindow> windows);

// Time between frames at `refresh_hz`. A rate SDL could not read (zero, negative, not finite) counts as 60 Hz.
[[nodiscard]] std::chrono::nanoseconds frame_period(float refresh_hz);

// How far apart FrameLimiter keeps frames after the draws. nullopt: no sleep, because a vsync swap already
// waited (`vsync_waited`), or vsync is off and `max_fps` is 0 (no cap). With vsync on and no window to wait
// on, the display's refresh rate is the limit and a lower `max_fps` still applies.
[[nodiscard]] std::optional<std::chrono::nanoseconds> limiter_period(
        bool vsync_enabled, bool vsync_waited, int max_fps, float refresh_hz);

}
