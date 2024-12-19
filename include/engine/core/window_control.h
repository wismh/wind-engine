#pragma once

#include <engine/core/window_desc.h>
#include <engine/render/commands.h>

#include <glm/vec2.hpp>

#include <optional>

namespace engine {

// Selects whether desktop-overlay behaviors run at all: synthetic cursor polling for
// click-through windows, per-window WS_EX_TRANSPARENT sync, and the Win32 modal-loop reentrant
// tick hook. Auto (the default) infers this from whether any live window is transparent — a game
// that wants an alpha-blended window without any of those OS hooks sets AlwaysDisabled; one that
// wants them running even before a transparent window exists yet sets AlwaysEnabled. Engine-wide,
// not per-window: one policy backs every window a game opens.
enum class OverlayMode {
    Auto,
    AlwaysEnabled,
    AlwaysDisabled
};

// Runtime window control a game asks for through DI (no service locator), backed by
// EngineRuntime's window(s). borderless/always_on_top can change after creation; transparent
// cannot (WindowStyle in window_desc.h) so it has no setter here.
class IWindowControl {
public:
    virtual ~IWindowControl() = default;

    // See OverlayMode above. Call before opening a transparent window if the game needs
    // AlwaysDisabled from the start — Auto would otherwise activate overlay hooks the instant that
    // window is created.
    virtual void set_overlay_mode(OverlayMode mode) = 0;
    [[nodiscard]] virtual OverlayMode overlay_mode() const = 0;

    // `window` (default kPrimaryWindow, trailing so every pre-existing call site keeps compiling
    // and behaving unchanged) generalizes these to the secondary windows opened via open_window()
    // below — a settings window can now toggle its own always_on_top or reposition
    // itself, not just the primary. A `window` with no live SDL window (not yet opened, or already
    // closed) makes these a no-op, same contract as calling them before the primary window exists.
    virtual void set_borderless(bool borderless, WindowId window = kPrimaryWindow) = 0;
    virtual void set_always_on_top(bool always_on_top, WindowId window = kPrimaryWindow) = 0;
    virtual void set_position(glm::ivec2 position, WindowId window = kPrimaryWindow) = 0;
    virtual void resize(glm::ivec2 size, WindowId window = kPrimaryWindow) = 0;

    // Manual on/off for click-through overlay mode; the automatic per-frame toggle only
    // runs while this is enabled (and the window is transparent). `window` generalizes to secondary
    // windows the same way as other controls.
    virtual void set_click_through_enabled(bool enabled, WindowId window = kPrimaryWindow) = 0;

    // Marks a region (window-client pixels, same coordinate space as UiCanvas.rect) draggable —
    // needed for a borderless window, which has no OS titlebar to drag by. nullopt
    // clears it. `window` generalizes to secondary windows the same way as other controls.
    //
    // Window movement is handled manually by the engine via mouse capture (SDL_CaptureMouse),
    // avoiding the OS's native modal move loop (DefWindowProc / HTCAPTION) and its associated
    // message starvation.
    //
    // WARNING: this is a raw geometry rectangle evaluated before UI event dispatch. Any left click
    // inside it initiates window dragging and is consumed immediately by the engine — no
    // button-down event reaches the UI or ECS input systems. Consequently, a Button placed inside
    // the drag rect (e.g. a titlebar close button next to or inside a drag handle) is unclickable.
    // Shrink or notch the rect to exclude every interactive control's bounds before calling this.
    virtual void set_drag_region(std::optional<render::Rect> region, WindowId window = kPrimaryWindow) = 0;

    // Opens/closes a secondary window. nullopt on failure (e.g. no primary
    // window yet). Closing is purely mechanical — see WindowCloseRequestedEvent
    // (include/engine/ui/canvas.h) for how a game learns a window's close button was clicked.
    virtual std::optional<WindowId> open_window(const WindowDesc& desc) = 0;
    virtual void close_window(WindowId id) = 0;

    // The display's usable area in screen pixels — the full display bounds minus OS chrome
    // (Windows taskbar, macOS menu bar/dock) — so a game can place a fixed-size overlay flush
    // against a screen edge without hardcoding a platform-specific work-area query itself.
    // `display_index` is 0-based into the platform's display list; out of range falls back
    // to the primary display. A zeroed Rect means the query failed (e.g. no video subsystem).
    [[nodiscard]] virtual render::Rect usable_display_bounds(int display_index = 0) const = 0;
};

}
