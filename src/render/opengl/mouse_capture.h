#pragma once

#include <engine/core/input_system.h>
#include <engine/core/window_desc.h>

#include <glm/vec2.hpp>

#include <optional>
#include <string_view>

namespace engine {

// SDL has one mouse capture for the whole process (SDL_CaptureMouse), but two parties want it: the engine while it
// moves a borderless window by its drag region, and a game that asked IWindowControl::capture_mouse. Each used to be
// able to switch the other off. This owns the single switch: the capture is on while any party wants it, and a
// party that lets go leaves the other's capture alone.
class MouseCapture {
public:
    // Turns the OS capture on or off and says whether the platform did it. SDL_CaptureMouse by default; a test passes
    // its own.
    using Apply = bool (*)(bool);

    explicit MouseCapture(Apply apply = nullptr) noexcept;

    // The engine's window move (WindowSystem::begin_drag_if_in_region / end_drag). Counted, not owned by a window.
    void begin_window_drag();
    void end_window_drag();

    // A game's request for `window`. One window at a time: asking for another window moves the capture. Returns
    // whether the capture is now in effect; on a platform that cannot capture it is false and nothing is held.
    // Releasing a window that does not hold the capture changes nothing and returns false.
    bool capture(WindowId window, bool on);

    [[nodiscard]] bool captured_by(WindowId window) const noexcept;
    // The OS capture is currently on, for either party.
    [[nodiscard]] bool active() const noexcept { return applied_; }

    // The window is gone: its capture goes with it.
    void window_closed(WindowId window);

private:
    void sync();

    Apply apply_;
    int window_drags_ = 0;
    std::optional<WindowId> game_owner_;
    bool applied_ = false;
};

// Whether the video driver can say where the cursor is on the desktop and which buttons are down while the cursor is
// outside the windows. Wayland does not let a client see the global pointer, and the browser and Android have no
// desktop. SDL reports window-relative values there, which would be wrong as screen coordinates.
[[nodiscard]] bool global_mouse_supported(std::string_view video_driver) noexcept;

// SDL_GetGlobalMouseState, or nullopt when the current video driver cannot (see above).
[[nodiscard]] std::optional<glm::vec2> global_cursor_position();
[[nodiscard]] bool global_mouse_button_down(MouseButton button);

}
