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
    // which has no OS titlebar to drag by. The SDL_SetWindowHitTest callback is installed
    // unconditionally in create() (every window, not just borderless ones — a bordered window's
    // titlebar already drags on its own, and the callback is inert outside a set region), so this
    // just updates the stored region the already-installed callback reads live. nullopt clears it.
    // No-op without a window (§12.3).
    void set_drag_region(std::optional<render::Rect> region) noexcept {
        drag_region_ = region;
    }

private:
    friend SDL_HitTestResult window_drag_hit_test(SDL_Window* window, const SDL_Point* area, void* data);

    SDL_Window* window_ = nullptr;
    bool transparent_ = false;
    bool click_through_enabled_ = false;
    bool click_through_applied_ = false;
    std::optional<render::Rect> drag_region_;

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

// SDL_HitTest callback installed on every window in WindowSystem::create() (SDD §21.7). `data` is
// the owning WindowSystem*; returns SDL_HITTEST_DRAGGABLE when `area` falls inside that window's
// current drag_region_, else SDL_HITTEST_NORMAL.
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
