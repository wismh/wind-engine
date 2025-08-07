#include "core/event_window.h"

namespace engine {

std::optional<WindowId> event_window(std::uint32_t sdl_window_id, std::optional<WindowId> live,
        NoWindowEvent no_window) noexcept {
    if (sdl_window_id != 0) {
        return live;
    }
    if (no_window == NoWindowEvent::Primary) {
        return kPrimaryWindow;
    }
    return std::nullopt;
}

}
