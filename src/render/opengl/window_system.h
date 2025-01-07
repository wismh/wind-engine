#pragma once

#include "gl_includes.h"

#include <engine/core/window_desc.h>
#include <engine/render/commands.h>
#include <engine/render/graphic_factory.h>

#include <glm/vec2.hpp>

#include <optional>

namespace engine {

class WindowSystem {
public:
    WindowSystem() = default;
    ~WindowSystem();

    WindowSystem(const WindowSystem&) = delete;
    WindowSystem& operator=(const WindowSystem&) = delete;

    [[nodiscard]] bool create(const WindowDesc& desc);
    void destroy();
    void swap() const;
    void set_icon(const render::TextureDesc& desc);

    // borderless/always-on-top/position/size can change after create(); WindowStyle::transparent
    // cannot (SDL has no "make an existing window transparent" call) so there is no setter for it.
    void set_bordered(bool bordered);
    void set_always_on_top(bool always_on_top);
    void set_position(glm::ivec2 position);
    void resize(glm::ivec2 size);

    [[nodiscard]] SDL_Window* window() const noexcept {
        return window_;
    }

    [[nodiscard]] glm::ivec2 size() const;
    [[nodiscard]] glm::ivec2 drawable_size() const;

    // Current OS cursor position in this window's client pixels, queried directly
    // (SDL_GetGlobalMouseState + SDL_GetWindowPosition) rather than read from the last delivered
    // SDL_EVENT_MOUSE_MOTION. SDD §21.7 regression fix: once click-through is actually applied
    // (WS_EX_TRANSPARENT set), Windows stops delivering WM_MOUSEMOVE for any point that hit-tests
    // as HTTRANSPARENT — including a point that later moves onto a widget — so a caller that only
    // ever reacts to real motion events can never learn the pointer came back over something
    // clickable. std::nullopt without a live window (§12.3). Assumes the window's OS position *is*
    // its client-area origin, true for the borderless windows this exists for (SDD §21.7 — a
    // bordered window's own titlebar drag doesn't need this workaround, see the caller).
    [[nodiscard]] std::optional<glm::vec2> cursor_client_position() const;

    [[nodiscard]] bool is_transparent() const noexcept {
        return transparent_;
    }

    // Manual on/off a settings-menu checkbox binds to (§21.4); does not itself touch the OS
    // window — update_click_through() applies it once per frame.
    void set_click_through_enabled(bool enabled) noexcept {
        click_through_enabled_ = enabled;
    }

    [[nodiscard]] bool click_through_enabled() const noexcept {
        return click_through_enabled_;
    }

    // True once apply_click_through(true) actually landed on the OS window (WS_EX_TRANSPARENT
    // currently set) — distinct from click_through_enabled(), the manual on/off toggle. The Win32
    // hit-test hook (window_drag_hit_test's caller in window_system.cpp, SDD §21.7 fix) reads this
    // to decide whether a point outside the drag region should resolve to HTTRANSPARENT instead of
    // the HTCLIENT SDL would otherwise hardcode once any SDL_HitTest callback is installed.
    [[nodiscard]] bool click_through_applied() const noexcept {
        return click_through_applied_;
    }

    // Called once per frame from EngineRuntime::tick_loop() with this frame's UiInputSystem hit
    // result. No-op without a window (§12.3 — no real window in engine_tests).
    void update_click_through(bool pointer_hit_something);

    // Marks a window-client-pixel region draggable (SDD §21.7) — needed for a borderless window,
    // which has no OS titlebar to drag by. nullopt clears it. No-op without a window (§12.3).
    // wind-92: no longer routed through the OS's own drag mechanism (SDL_HITTEST_DRAGGABLE/
    // HTCAPTION) — see begin_drag_if_in_region()'s doc comment for why — so this purely updates
    // the stored rect that helper (and the click-through exclusion in update_click_through()/
    // win32_hit_test_wndproc) read live.
    void set_drag_region(std::optional<render::Rect> region) noexcept {
        drag_region_ = region;
    }

    // wind-92 (SDD §21.7): starts a manually-implemented drag if `window_local_pos` (client
    // pixels, e.g. from an SDL_EVENT_MOUSE_BUTTON_DOWN's x/y) falls inside drag_region_, deliberately
    // *without* ever going through the OS's native HTCAPTION/DefWindowProc-modal-loop path the
    // engine used before. That path — reached by returning SDL_HITTEST_DRAGGABLE from a window's
    // SDL_HitTest callback, which Windows turns into HTCAPTION — hands control of the calling
    // thread to DefWindowProc's own modal move loop until the mouse button is released, and
    // wind-88 through wind-91 each worked around a different downstream symptom of that same
    // handoff (frozen rendering, then unreliable WM_TIMER delivery specifically for an opaque
    // window, confirmed by measurement — SDD §21.7) rather than avoiding it. This avoids it
    // entirely for a drag-region-initiated drag: captures the mouse (SDL_CaptureMouse) so motion
    // keeps arriving even once the cursor leaves the window's bounds, remembers the window's and
    // cursor's starting position, and update_drag()/end_drag() do the rest through ordinary
    // SDL_SetWindowPosition calls on the normal event-pumped thread — no modal loop, no
    // reentrancy, none of the machinery §21.7's earlier fixes needed. Only ever call this on a
    // left-button-down (matching the old HTCAPTION convention); returns true if a drag actually
    // started, in which case the caller should treat the triggering click as fully consumed — the
    // app never saw a button-down for an HTCAPTION click either. Still doesn't help a *bordered*
    // window's real OS titlebar or a live-resize via WS_THICKFRAME borders — both still enter the
    // true OS modal loop regardless of anything here, so §21.7's reentrant-tick fixes remain
    // load-bearing for those.
    [[nodiscard]] bool begin_drag_if_in_region(glm::vec2 window_local_pos);

    // Moves the window to track the cursor — call once per SDL_EVENT_MOUSE_MOTION while
    // is_dragging() is true. No-op if not currently dragging or without a window (§12.3).
    void update_drag();

    // Ends a drag started by begin_drag_if_in_region(), releasing mouse capture. Safe to call even
    // when not currently dragging (no-op) — e.g. as a focus-loss safety net so a drag can never
    // get stuck active if a button-up is somehow missed.
    void end_drag();

    [[nodiscard]] bool is_dragging() const noexcept {
        return dragging_;
    }

private:
    friend SDL_HitTestResult window_drag_hit_test(SDL_Window* window, const SDL_Point* area, void* data);

    SDL_Window* window_ = nullptr;
    bool transparent_ = false;
    bool click_through_enabled_ = false;
    bool click_through_applied_ = false;
    std::optional<render::Rect> drag_region_;
    bool dragging_ = false;                       // wind-92
    glm::ivec2 drag_start_cursor_screen_{};        // wind-92, valid only while dragging_
    glm::ivec2 drag_start_window_pos_{};           // wind-92, valid only while dragging_

#if defined(_WIN32)
public:
    // Original SDL window procedure (WIN_WindowProc), captured by create() before it subclasses
    // the HWND with win32_hit_test_wndproc (window_system.cpp, SDD §21.7 fix) on top of it — the
    // subclass needs it to delegate every message it doesn't special-case. Stored as void* (real
    // type WNDPROC) purely so this header never needs <windows.h> (SDD §16 rule 15 — keeps
    // windows.h macro pollution, e.g. min/max, out of every other TU that includes this private
    // header); window_system.cpp does the cast both ways. Not a general-purpose accessor — read
    // only by the free function installed as the HWND's GWLP_WNDPROC.
    [[nodiscard]] void* win32_prev_wndproc() const noexcept {
        return win32_prev_wndproc_;
    }

private:
    void* win32_prev_wndproc_ = nullptr;
#endif

    void apply_click_through(bool click_through);
};

// Pure "is this point inside the drag region" geometry check (SDD §21.7) — `data` is the owning
// WindowSystem*; returns SDL_HITTEST_DRAGGABLE when `area` falls inside that window's current
// drag_region_, else SDL_HITTEST_NORMAL. Despite the SDL_HitTest-shaped signature (convenient,
// nothing more), wind-92 stopped registering this via SDL_SetWindowHitTest — it's called directly
// as a plain membership test instead, by begin_drag_if_in_region() (window_system.cpp) and by the
// click-through exclusion in win32_hit_test_wndproc/update_click_through() (same file).
[[nodiscard]] SDL_HitTestResult window_drag_hit_test(SDL_Window* window, const SDL_Point* area, void* data);

// Split out from create() so WindowStyle -> SDL_WindowFlags is unit-testable without
// SDL_Init(SDL_INIT_VIDEO) or a real window.
[[nodiscard]] SDL_WindowFlags window_style_flags(const WindowStyle& style);

// Pure decision half of §21.4 click-through: bounding-box only, no per-pixel alpha sampling
// (that needs a synced framebuffer readback, deferred — SDD §17).
[[nodiscard]] bool should_be_click_through(
        bool click_through_enabled, bool window_is_transparent, bool pointer_hit_something) noexcept;

// Split out from set_icon() so the TextureDesc -> SDL_Surface byte layout is unit-testable
// without SDL_Init(SDL_INIT_VIDEO) or a real window. Caller owns the returned surface.
[[nodiscard]] SDL_Surface* make_icon_surface(const render::TextureDesc& desc);

}
