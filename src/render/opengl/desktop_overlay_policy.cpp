#include "desktop_overlay_policy.h"

#include "window_manager.h"
#include <engine/core/input_system.h>
#include <engine/ui/canvas.h>

namespace engine {

bool DesktopOverlayPolicy::has_active_overlay(const WindowManager& windows) const {
    if (mode_ == OverlayMode::AlwaysDisabled) {
        return false;
    }
    if (mode_ == OverlayMode::AlwaysEnabled) {
        return true;
    }
    bool active = false;
    windows.for_each_window([&active](WindowId, const WindowSystem& window) {
        if (window.is_transparent()) {
            active = true;
        }
    });
    return active;
}

void DesktopOverlayPolicy::poll_cursor(WindowManager& windows, InputSystem& input) {
    if (!has_active_overlay(windows)) {
        return;
    }
    windows.for_each_window([&](WindowId id, WindowSystem& window) {
        if (window.click_through_enabled() && window.is_transparent()) {
            if (const std::optional<glm::vec2> cursor = window.cursor_client_position()) {
                input.handle_mouse_move(id, *cursor, glm::vec2{0.0f, 0.0f});
            }
        }
    });
}

void DesktopOverlayPolicy::update_click_through(WindowManager& windows, const ui::MouseConsumed& consumed) {
    if (!has_active_overlay(windows)) {
        return;
    }
    windows.for_each_window([&](WindowId id, WindowSystem& window) {
        window.update_click_through(consumed.consumed_for(id));
    });
}

void DesktopOverlayPolicy::sync_modal_loop_hook(WindowManager& windows, std::function<void()> tick_callback) {
    if (tick_callback && has_active_overlay(windows)) {
        windows.set_modal_loop_tick_callback(std::move(tick_callback));
    } else {
        windows.set_modal_loop_tick_callback(nullptr);
    }
}

} // namespace engine
