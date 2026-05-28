#include "core/frame_pacing.h"

#include <algorithm>
#include <cmath>

namespace engine {

std::optional<WindowId> choose_vsync_window(std::span<const PacingWindow> windows) {
    std::optional<WindowId> chosen;
    for (const PacingWindow& window : windows) {
        if (!window.presentable || !window.vsync_supported) {
            continue;
        }
        if (window.id == kPrimaryWindow) {
            return window.id;
        }
        if (!chosen || window.id < *chosen) {
            chosen = window.id;
        }
    }
    return chosen;
}

std::chrono::nanoseconds frame_period(float refresh_hz) {
    constexpr float kFallbackHz = 60.0f;
    const float hz = std::isfinite(refresh_hz) && refresh_hz > 0.0f ? refresh_hz : kFallbackHz;
    return std::chrono::nanoseconds{static_cast<std::chrono::nanoseconds::rep>(std::llround(1.0e9 / hz))};
}

std::optional<std::chrono::nanoseconds> limiter_period(
        bool vsync_enabled, bool vsync_waited, int max_fps, float refresh_hz) {
    if (vsync_waited) {
        return std::nullopt;
    }
    const std::optional<std::chrono::nanoseconds> cap =
            max_fps > 0 ? std::optional{frame_period(static_cast<float>(max_fps))} : std::nullopt;
    if (!vsync_enabled) {
        return cap;
    }
    const std::chrono::nanoseconds display = frame_period(refresh_hz);
    return cap ? std::max(*cap, display) : display;
}

}
