#pragma once

// docs/tech/features/Windowing.md#events-of-a-window

#include <engine/core/window_desc.h>

#include <cstdint>
#include <optional>

namespace engine {

// What an SDL event with window id 0 (no window) means for one kind of event.
enum class NoWindowEvent {
    // Dropped. Window events (resize, close, focus): SDL always sends them with the window's own id.
    Drop,
    // kPrimaryWindow. Keyboard, text, and mouse events carry 0 while no window has focus; on a single-window
    // platform (a web release outside the canvas, an Android key) that input is the primary window's.
    Primary,
};

// The window an SDL event goes to. `sdl_window_id` is the event's windowID; `live` is the window that SDL id
// belongs to among the live windows (WindowManager::find_by_sdl_id). A non-zero id of no live window (an event
// still queued for a window closed this frame) is dropped: nullopt. Id 0 follows `no_window`.
[[nodiscard]] std::optional<WindowId> event_window(std::uint32_t sdl_window_id, std::optional<WindowId> live,
        NoWindowEvent no_window) noexcept;

}
