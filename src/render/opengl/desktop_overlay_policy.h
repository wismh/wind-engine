#pragma once

#include <engine/core/window_control.h>

#include <functional>

namespace engine {

class WindowManager;
class InputSystem;

namespace ui {
struct MouseConsumed;
}

// Encapsulates desktop overlay specific behavior (cursor polling for click-through windows,
// per-window WS_EX_TRANSPARENT updates, and Win32 modal loop message hook synchronization)
// so the primary game loop remains standard, deterministic, and free of overlay overhead
// when running standard or opaque games.
class DesktopOverlayPolicy {
public:
    explicit DesktopOverlayPolicy(OverlayMode mode = OverlayMode::Auto) noexcept
        : mode_(mode) {}

    [[nodiscard]] OverlayMode mode() const noexcept { return mode_; }
    void set_mode(OverlayMode mode) noexcept { mode_ = mode; }

    // Returns true if an active desktop overlay is present. Under Auto mode, this is true if any
    // live window is transparent. AlwaysEnabled/AlwaysDisabled override automatic detection.
    [[nodiscard]] bool has_active_overlay(const WindowManager& windows) const;

    // Direct cursor polling for transparent click-through windows to synthesize motion events.
    // No-op if has_active_overlay() is false.
    void poll_cursor(WindowManager& windows, InputSystem& input);

    // Updates WS_EX_TRANSPARENT per-window based on UI mouse consumption.
    // No-op if has_active_overlay() is false.
    void update_click_through(WindowManager& windows, const ui::MouseConsumed& consumed);

    // Registers or unregisters WindowManager's Win32 modal loop tick callback based on whether
    // an active overlay is present.
    void sync_modal_loop_hook(WindowManager& windows, std::function<void()> tick_callback);

private:
    OverlayMode mode_ = OverlayMode::Auto;
};

} // namespace engine
