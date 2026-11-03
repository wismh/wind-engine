#pragma once

// docs/tech/features/Windowing.md (Relative mouse)

#include <engine/core/window_desc.h>

#include <functional>
#include <unordered_set>

namespace engine {

// A game's relative mouse requests (IWindowControl::set_relative_mouse) and the focus rule over them: the mode is on
// for a window only while the game asked for it and the window has focus. Losing focus switches it off and keeps the
// request; regaining focus switches it on again. SDL3 has the same rule internally, but the engine keeps its own so
// every backend (the browser's pointer lock included) behaves alike, `is_relative_mouse` is the game's request rather
// than SDL's flag, and the rule is tested without a window.
class RelativeMouse {
public:
    // Switches the OS mode for one window. false: the platform refused (Android, the browser without the mouse over
    // the canvas). The window manager passes SDL_SetWindowRelativeMouseMode; a test passes a recorder.
    using Apply = std::function<bool(WindowId, bool)>;

    explicit RelativeMouse(Apply apply);

    // on: applies the mode now. A refusal drops the request and returns false. off: forgets the request, switches
    // the mode off if it was on, and returns false.
    bool request(WindowId window, bool on);

    [[nodiscard]] bool requested(WindowId window) const noexcept;
    // The mode is on now: requested, applied, and the window did not lose focus since.
    [[nodiscard]] bool active(WindowId window) const noexcept;

    // Switches the mode off; the request stays.
    void focus_lost(WindowId window);
    // Switches the mode on again when it was requested. A refusal keeps the request for the next focus gain.
    void focus_gained(WindowId window);
    // The window is gone: forgets it without calling Apply.
    void window_closed(WindowId window);

private:
    Apply apply_;
    std::unordered_set<WindowId> requested_;
    std::unordered_set<WindowId> active_;
};

}
