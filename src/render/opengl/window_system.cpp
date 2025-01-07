#include "window_system.h"

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace engine {

#if defined(_WIN32)
namespace {

// SDD §21.7 fix: WIN_WindowProc (external/SDL3/src/video/windows/SDL_windowsevents.c) maps
// SDL_HITTEST_NORMAL to HTCLIENT unconditionally once any SDL_HitTest callback is installed, and
// never falls through to DefWindowProc for that message. DefWindowProc is the only place that
// inspects WS_EX_TRANSPARENT and would return HTTRANSPARENT for it (the mechanism
// apply_click_through's doc comment relies on) — so a hit_test callback shuts off real
// click-through entirely, everywhere, the moment one is ever installed on a window.
// SDL_HitTestResult also has no HTTRANSPARENT-equivalent value, so this can't be fixed through
// the public SDL_HitTest callback alone (reported upstream:
// https://github.com/libsdl-org/SDL — no way to yield to DefWindowProc / express a transparent
// hit region). Instead, this subclasses the HWND on top of SDL's own WIN_WindowProc (already
// installed by SDL_CreateWindow, see SDL_windowswindow.c's WIN_CreateWindow) and intercepts only
// WM_NCHITTEST: when click-through is currently applied and the point falls outside the drag
// region, it returns HTTRANSPARENT directly, bypassing SDL entirely for that one message. Every
// other case delegates to the saved original proc — which, since wind-92 stopped calling
// SDL_SetWindowHitTest at all (create() below no longer installs window_drag_hit_test as an
// SDL_HitTest callback; drag-region clicks are handled manually instead, never reaching
// WM_NCHITTEST as HTCAPTION in the first place — see begin_drag_if_in_region()), now means
// `window->hit_test` is null and WIN_WindowProc's own hit-test branch is never taken, so this
// falls all the way through to DefWindowProc's own default per-style hit-testing — exactly what a
// borderless window with no active click-through wants (plain HTCLIENT), and, as a side effect,
// restores a *bordered* window's real OS titlebar to DefWindowProc's native recognition instead of
// the SDL_HITTEST_NORMAL-always-means-HTCLIENT override installing any hit_test callback used to
// impose on every point regardless of style.
LRESULT CALLBACK win32_hit_test_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<WindowSystem*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    auto prev = reinterpret_cast<WNDPROC>(self != nullptr ? self->win32_prev_wndproc() : nullptr);

    if (msg == WM_NCHITTEST && self != nullptr && self->click_through_applied()) {
        POINT pt{static_cast<LONG>(static_cast<short>(LOWORD(lParam))),
                static_cast<LONG>(static_cast<short>(HIWORD(lParam)))};
        if (ScreenToClient(hwnd, &pt)) {
            const SDL_Point area{static_cast<int>(pt.x), static_cast<int>(pt.y)};
            // window_drag_hit_test ignores its `window` parameter (only reads drag_region_ off
            // `self`), so passing nullptr here is safe — same call SDL itself makes internally.
            if (window_drag_hit_test(nullptr, &area, self) == SDL_HITTEST_NORMAL) {
                return HTTRANSPARENT;
            }
        }
    }

    return prev != nullptr ? CallWindowProcW(prev, hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

}
#endif

WindowSystem::~WindowSystem() {
    destroy();
}

bool WindowSystem::create(const WindowDesc& desc) {
    destroy();

#if defined(ENGINE_WITH_GLES)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    // SDL_WINDOW_TRANSPARENT alone doesn't get DWM an alpha channel to composite on Windows: the
    // GL pixel format picked at SDL_CreateWindow time only carries alpha if SDL_GL_ALPHA_SIZE was
    // requested first (confirmed from external/SDL3/src/video/windows/SDL_windowsopengl.c, which
    // never looks at SDL_WINDOW_TRANSPARENT itself, only gl_config.alpha_size). Opaque windows are
    // left alone to avoid any pixel-format regression there.
    if (desc.style.transparent) {
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    }
    // SDL3's Windows backend defaults SDL_WINDOW_BORDERLESS to a "borderless-windowed" style that
    // still keeps WS_CAPTION/WS_SYSMENU (external/SDL3/src/video/windows/SDL_windowswindow.c,
    // GetWindowStyle()/STYLE_BORDERLESS_WINDOWED) — i.e. Windows still draws a titlebar — on
    // purpose, so a borderless window keeps acting like a normal desktop citizen (taskbar,
    // work-area clamping). A desktop-overlay window wants the opposite: no titlebar at all. The
    // hint has no public SDL_HINT_* constant (raw string, same one GetWindowStyle() reads); it's
    // read once per SDL_CreateWindow call, so setting it right before this one call is enough — it
    // has no effect at all on a non-borderless window (that branch is never taken for one).
    if (desc.style.borderless) {
        SDL_SetHint("SDL_BORDERLESS_WINDOWED_STYLE", "0");
    }

    const SDL_WindowFlags flags = SDL_WINDOW_OPENGL | window_style_flags(desc.style);
    window_ = SDL_CreateWindow(desc.title.c_str(), desc.size.x, desc.size.y, flags);
    if (window_ == nullptr) {
        return false;
    }
    transparent_ = desc.style.transparent;
    if (desc.position) {
        SDL_SetWindowPosition(window_, desc.position->x, desc.position->y);
    }
    // wind-92 (SDD §21.7): no SDL_SetWindowHitTest call here anymore — a drag-region click is
    // handled manually (begin_drag_if_in_region()) instead of being routed through the OS's own
    // HTCAPTION/modal-loop drag, so nothing needs SDL to know about drag_region_ at the
    // hit-testing level at all. window_drag_hit_test still exists as a plain, directly-called
    // geometry check (see its own doc comment).
#if defined(_WIN32)
    // SDD §21.7 fix (see win32_hit_test_wndproc's doc comment above): subclass on top of SDL's own
    // WIN_WindowProc so WM_NCHITTEST can resolve to HTTRANSPARENT for click-through, which SDL's
    // own hit-test dispatch can never produce. GWLP_USERDATA is free for a normal top-level window
    // — SDL only ever uses it for its message-box dialogs and tray-icon windows (SDL_windowsmessagebox.c,
    // SDL_tray.c), never for a window created via SDL_CreateWindow, so this doesn't collide with
    // SDL's own bookkeeping (that lives in a window property, "SDL_WindowData", read via
    // WIN_GetWindowDataFromHWND — untouched here). SetWindowLongPtrW's return value is the
    // previously-installed WNDPROC (SDL's WIN_WindowProc), saved so the subclass can delegate.
    if (HWND hwnd = static_cast<HWND>(
                SDL_GetPointerProperty(SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
            hwnd != nullptr) {
        // SDD §21.4 regression fix (td-over): WS_EX_TRANSPARENT alone does not give real,
        // OS-delivered click-through on a DWM-composited window (one using SDL_WINDOW_TRANSPARENT
        // + DwmEnableBlurBehindWindow, not the classic WS_EX_LAYERED alpha-blend path) — confirmed
        // empirically (samples/overlay_probe investigation, not reproducible from engine_tests
        // per §12.3): with WS_EX_TRANSPARENT set but WS_EX_LAYERED never added, WM_NCHITTEST
        // answers HTTRANSPARENT correctly but the *real* click still doesn't reach whatever is
        // underneath. Adding WS_EX_LAYERED alone regressed further: with it added but never
        // "armed" via SetLayeredWindowAttributes/UpdateLayeredWindow, its alpha/blend state is
        // undefined, and Windows then appears to treat the *entire* window as input-transparent —
        // not just the points win32_hit_test_wndproc answers HTTRANSPARENT for — breaking the
        // drag region and, with it, keyboard focus (a click never lands on the window to activate
        // it). SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA) arms it at full opacity on the
        // classic GDI blend path; the actual visual transparency still comes from DWM's
        // DwmEnableBlurBehindWindow above, untouched by this. Only for a transparent window —
        // WS_EX_LAYERED has no reason to exist on an opaque one.
        if (transparent_) {
            const LONG_PTR ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex_style | WS_EX_LAYERED);
            SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        win32_prev_wndproc_ = reinterpret_cast<void*>(
                SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&win32_hit_test_wndproc)));
    }
#endif
    return true;
}

void WindowSystem::destroy() {
    transparent_ = false;
    click_through_applied_ = false;
    drag_region_.reset();
    // wind-92: releases capture (harmless if it was never held) rather than leaving a captured
    // mouse dangling past the window it was captured for.
    if (dragging_) {
        end_drag();
    }
#if defined(_WIN32)
    win32_prev_wndproc_ = nullptr;
#endif
    if (window_ == nullptr) {
        return;
    }
    SDL_DestroyWindow(window_);
    window_ = nullptr;
}

void WindowSystem::swap() const {
    if (window_ != nullptr) {
        SDL_GL_SwapWindow(window_);
    }
}

void WindowSystem::set_bordered(bool bordered) {
    if (window_ != nullptr) {
        SDL_SetWindowBordered(window_, bordered);
    }
}

void WindowSystem::set_always_on_top(bool always_on_top) {
    if (window_ != nullptr) {
        SDL_SetWindowAlwaysOnTop(window_, always_on_top);
    }
}

void WindowSystem::set_position(glm::ivec2 position) {
    if (window_ != nullptr) {
        SDL_SetWindowPosition(window_, position.x, position.y);
    }
}

void WindowSystem::resize(glm::ivec2 size) {
    if (window_ != nullptr) {
        SDL_SetWindowSize(window_, size.x, size.y);
    }
}

void WindowSystem::update_click_through(bool pointer_hit_something) {
    if (window_ == nullptr) {
        return;
    }
    // SDD §21.4/§21.7 regression fix (td-over): the drag region must count as "hit" for this
    // decision too, not just MouseConsumed from UI hit-testing — otherwise WS_EX_TRANSPARENT stays
    // applied for the whole time the pointer sits in the drag region (a game's drag strip is not
    // necessarily backed by an actual UI widget that would set MouseConsumed on its own). That
    // matters because real click delivery for this layered+transparent window turned out to
    // depend on whether WS_EX_TRANSPARENT is set on the window *at all* at the moment of the real
    // click, not on win32_hit_test_wndproc's correct-but-DWM-ignored per-point HTCAPTION answer —
    // confirmed empirically (samples/overlay_probe investigation): with the drag region excluded
    // from MouseConsumed but not from this decision, WM_NCHITTEST answered HTCAPTION correctly for
    // every drag-region point yet the real drag gesture (and, with it, keyboard focus) still
    // failed, because the window was still marked transparent when the actual mouse-down landed.
    bool effective_hit = pointer_hit_something;
    if (const std::optional<glm::vec2> cursor = cursor_client_position()) {
        const SDL_Point point{static_cast<int>(cursor->x), static_cast<int>(cursor->y)};
        // window_drag_hit_test ignores its `window` parameter (only reads drag_region_ off
        // `this`), so passing nullptr here is safe — same call win32_hit_test_wndproc makes.
        if (window_drag_hit_test(nullptr, &point, this) == SDL_HITTEST_DRAGGABLE) {
            effective_hit = true;
        }
    }
    const bool click_through = should_be_click_through(click_through_enabled_, transparent_, effective_hit);
    if (click_through == click_through_applied_) {
        return;
    }
    apply_click_through(click_through);
    click_through_applied_ = click_through;
}

#if defined(_WIN32)
void WindowSystem::apply_click_through(bool click_through) {
    HWND hwnd = static_cast<HWND>(
            SDL_GetPointerProperty(SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (hwnd == nullptr) {
        return;
    }
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    // Toggles WS_EX_TRANSPARENT only — WS_EX_LAYERED (needed alongside it for real click-through
    // on this DWM-composited window, see create()'s doc comment) is armed once, at creation, and
    // stays set for the transparent window's whole lifetime; it never needs toggling here.
    const LONG_PTR updated = click_through ? (style | WS_EX_TRANSPARENT) : (style & ~WS_EX_TRANSPARENT);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, updated);
}
#else
void WindowSystem::apply_click_through(bool /*click_through*/) {}
#endif

void WindowSystem::set_icon(const render::TextureDesc& desc) {
    if (window_ == nullptr) {
        return;
    }
    SDL_Surface* surface = make_icon_surface(desc);
    if (surface == nullptr) {
        return;
    }
    SDL_SetWindowIcon(window_, surface);
    SDL_DestroySurface(surface);
}

glm::ivec2 WindowSystem::size() const {
    int width = 0;
    int height = 0;
    if (window_ != nullptr) {
        SDL_GetWindowSize(window_, &width, &height);
    }
    return {width, height};
}

std::optional<glm::vec2> WindowSystem::cursor_client_position() const {
    if (window_ == nullptr) {
        return std::nullopt;
    }
    float global_x = 0.0f;
    float global_y = 0.0f;
    SDL_GetGlobalMouseState(&global_x, &global_y);
    int window_x = 0;
    int window_y = 0;
    SDL_GetWindowPosition(window_, &window_x, &window_y);
    return glm::vec2{global_x - static_cast<float>(window_x), global_y - static_cast<float>(window_y)};
}

bool WindowSystem::begin_drag_if_in_region(glm::vec2 window_local_pos) {
    if (window_ == nullptr) {
        return false;
    }
    const SDL_Point point{static_cast<int>(window_local_pos.x), static_cast<int>(window_local_pos.y)};
    // window_drag_hit_test ignores its `window` parameter (only reads drag_region_ off `this`), so
    // passing nullptr here is safe — same call win32_hit_test_wndproc already makes.
    if (window_drag_hit_test(nullptr, &point, this) != SDL_HITTEST_DRAGGABLE) {
        return false;
    }
    float global_x = 0.0f;
    float global_y = 0.0f;
    SDL_GetGlobalMouseState(&global_x, &global_y);
    drag_start_cursor_screen_ = glm::ivec2{static_cast<int>(global_x), static_cast<int>(global_y)};
    int window_x = 0;
    int window_y = 0;
    SDL_GetWindowPosition(window_, &window_x, &window_y);
    drag_start_window_pos_ = glm::ivec2{window_x, window_y};
    SDL_CaptureMouse(true);
    dragging_ = true;
    return true;
}

void WindowSystem::update_drag() {
    if (!dragging_ || window_ == nullptr) {
        return;
    }
    float global_x = 0.0f;
    float global_y = 0.0f;
    SDL_GetGlobalMouseState(&global_x, &global_y);
    const int delta_x = static_cast<int>(global_x) - drag_start_cursor_screen_.x;
    const int delta_y = static_cast<int>(global_y) - drag_start_cursor_screen_.y;
    SDL_SetWindowPosition(window_, drag_start_window_pos_.x + delta_x, drag_start_window_pos_.y + delta_y);
}

void WindowSystem::end_drag() {
    if (!dragging_) {
        return;
    }
    dragging_ = false;
    SDL_CaptureMouse(false);
}

glm::ivec2 WindowSystem::drawable_size() const {
    int width = 0;
    int height = 0;
    if (window_ != nullptr) {
        SDL_GetWindowSizeInPixels(window_, &width, &height);
    }
    return {width, height};
}

SDL_WindowFlags window_style_flags(const WindowStyle& style) {
    SDL_WindowFlags flags = 0;
    if (style.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (style.borderless) {
        flags |= SDL_WINDOW_BORDERLESS;
    }
    if (style.always_on_top) {
        flags |= SDL_WINDOW_ALWAYS_ON_TOP;
    }
    if (style.transparent) {
        flags |= SDL_WINDOW_TRANSPARENT;
    }
    return flags;
}

bool should_be_click_through(bool click_through_enabled, bool window_is_transparent, bool pointer_hit_something) noexcept {
    return click_through_enabled && window_is_transparent && !pointer_hit_something;
}

SDL_HitTestResult window_drag_hit_test(SDL_Window* /*window*/, const SDL_Point* area, void* data) {
    const auto* self = static_cast<const WindowSystem*>(data);
    if (self == nullptr || area == nullptr || !self->drag_region_) {
        return SDL_HITTEST_NORMAL;
    }
    const render::Rect& region = *self->drag_region_;
    const bool inside = static_cast<float>(area->x) >= region.x && static_cast<float>(area->y) >= region.y &&
                         static_cast<float>(area->x) < (region.x + region.w) &&
                         static_cast<float>(area->y) < (region.y + region.h);
    return inside ? SDL_HITTEST_DRAGGABLE : SDL_HITTEST_NORMAL;
}

SDL_Surface* make_icon_surface(const render::TextureDesc& desc) {
    const std::size_t required =
            static_cast<std::size_t>(desc.width) * static_cast<std::size_t>(desc.height) * 4;
    if (desc.width <= 0 || desc.height <= 0 || desc.rgba.size() < required) {
        return nullptr;
    }
    // SDL3 wants a non-const pixel pointer even though SDL_SetWindowIcon only reads from it
    // (it copies the pixels before returning), so this cast does not violate desc's constness.
    return SDL_CreateSurfaceFrom(desc.width, desc.height, SDL_PIXELFORMAT_RGBA32,
            const_cast<std::uint8_t*>(desc.rgba.data()), desc.width * 4);
}

}
