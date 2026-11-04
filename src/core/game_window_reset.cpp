#include "core/game_window_reset.h"

#include <engine/core/window_control.h>

#include <optional>

namespace engine {

void reset_game_windows(IWindowControl& windows) {
    windows.set_drag_region(std::nullopt);
    windows.set_click_through_enabled(false);
    windows.set_overlay_mode(OverlayMode::Auto);
    for (const WindowId id : windows.open_windows()) {
        (void)windows.set_relative_mouse(id, false);
        (void)windows.capture_mouse(id, false);
    }
}

}
