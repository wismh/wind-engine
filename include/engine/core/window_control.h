#pragma once

#include <engine/core/window_desc.h>
#include <engine/render/commands.h>

#include <glm/vec2.hpp>

#include <optional>

namespace engine {

// Runtime window control a game asks for through DI (SDD §4.2 — no service locator), backed by
// EngineRuntime's window(s). borderless/always_on_top can change after creation; transparent
// cannot (WindowStyle in window_desc.h, §21.2) so it has no setter here.
class IWindowControl {
public:
    virtual ~IWindowControl() = default;

    // `window` (default kPrimaryWindow, trailing so every pre-existing call site keeps compiling
    // and behaving unchanged) generalizes these to the secondary windows opened via open_window()
    // below (SDD §21.7) — a settings window can now toggle its own always_on_top or reposition
    // itself, not just the primary. A `window` with no live SDL window (not yet opened, or already
    // closed) makes these a no-op, same contract as calling them before the primary window exists.
    virtual void set_borderless(bool borderless, WindowId window = kPrimaryWindow) = 0;
    virtual void set_always_on_top(bool always_on_top, WindowId window = kPrimaryWindow) = 0;
    virtual void set_position(glm::ivec2 position, WindowId window = kPrimaryWindow) = 0;
    virtual void resize(glm::ivec2 size, WindowId window = kPrimaryWindow) = 0;

    // Manual on/off for §21.4 click-through overlay mode; the automatic per-frame toggle only
    // runs while this is enabled (and the window is transparent). Click-through stays a
    // kPrimaryWindow-only concept (§21.4/§21.7) — EngineRuntime::tick_loop() only ever drives it
    // for the primary window — so this one has no WindowId parameter.
    virtual void set_click_through_enabled(bool enabled) = 0;

    // Marks a region (window-client pixels, same space as UiCanvas.rect) draggable via
    // SDL_SetWindowHitTest — needed for a borderless window, which has no OS titlebar to drag by
    // (SDD §21.7). nullopt clears it. `window` generalizes to secondary windows the same way as
    // above — a borderless secondary window needs its own drag region just as much as the primary.
    //
    // WARNING: this is a raw rectangle with no knowledge of the UI tree. Any click inside it is
    // reported to the OS as HTCAPTION (SDL_HITTEST_DRAGGABLE), which becomes a non-client
    // WM_NCLBUTTONDOWN — the engine never sees SDL_EVENT_MOUSE_BUTTON_DOWN/_UP for it, so a Button
    // placed inside the drag rect (e.g. a titlebar close button next to the drag handle) is
    // silently unclickable no matter what it's bound to. Shrink or notch the rect yourself to
    // exclude every interactive control's bounds before calling this.
    virtual void set_drag_region(std::optional<render::Rect> region, WindowId window = kPrimaryWindow) = 0;

    // Opens/closes a secondary window (SDD §21.5/§21.7). nullopt on failure (e.g. no primary
    // window yet). Closing is purely mechanical — see WindowCloseRequestedEvent
    // (include/engine/ui/canvas.h) for how a game learns a window's close button was clicked.
    virtual std::optional<WindowId> open_window(const WindowDesc& desc) = 0;
    virtual void close_window(WindowId id) = 0;

    // The display's usable area in screen pixels — the full display bounds minus OS chrome
    // (Windows taskbar, macOS menu bar/dock) — so a game can place a fixed-size overlay flush
    // against a screen edge without hardcoding a platform-specific work-area query itself (SDD
    // §21.2). `display_index` is 0-based into the platform's display list; out of range falls back
    // to the primary display. A zeroed Rect means the query failed (e.g. no video subsystem).
    [[nodiscard]] virtual render::Rect usable_display_bounds(int display_index = 0) const = 0;
};

}
