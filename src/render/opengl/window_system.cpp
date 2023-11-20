#include "window_system.h"

#include "mouse_capture.h"

#include <engine/log.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace engine {

#if defined(_WIN32)
namespace {

// WIN_WindowProc (external/SDL3/src/video/windows/SDL_windowsevents.c) maps
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
// other case delegates to the saved original proc — drag-region clicks are handled manually
// instead (never reaching WM_NCHITTEST as HTCAPTION in the first place — see
// begin_drag_if_in_region()), so `window->hit_test` is null and WIN_WindowProc's own hit-test
// branch is never taken, so this falls all the way through to DefWindowProc's own default per-style
// hit-testing — exactly what a borderless window with no active click-through wants (plain
// HTCLIENT), and, as a side effect, restores a *bordered* window's real OS titlebar to
// DefWindowProc's native recognition instead of the SDL_HITTEST_NORMAL-always-means-HTCLIENT
// override installing any hit_test callback used to impose on every point regardless of style.
LRESULT CALLBACK win32_hit_test_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<WindowSystem*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    auto prev = reinterpret_cast<WNDPROC>(self != nullptr ? self->win32_prev_wndproc() : nullptr);

    if (msg == WM_NCCALCSIZE && wParam == TRUE && self != nullptr && self->is_borderless() && !self->is_transparent()) {
        if (self->is_maximized()) {
            HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            if (monitor != nullptr) {
                MONITORINFO mi{};
                mi.cbSize = sizeof(mi);
                if (GetMonitorInfoW(monitor, &mi)) {
                    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                    params->rgrc[0] = mi.rcWork;
                    return 0;
                }
            }
        }
        return 0;
    }

    if (msg == WM_NCHITTEST && self != nullptr) {
        const int screen_x = static_cast<short>(LOWORD(lParam));
        const int screen_y = static_cast<short>(HIWORD(lParam));

        if (self->click_through_applied()) {
            POINT pt{screen_x, screen_y};
            if (ScreenToClient(hwnd, &pt)) {
                if (!self->is_in_drag_region(glm::vec2{static_cast<float>(pt.x), static_cast<float>(pt.y)})) {
                    return HTTRANSPARENT;
                }
            }
        }

        if (self->is_borderless() && !self->is_transparent()) {
            const bool maximized = self->is_maximized();

            if (self->is_resizable() && !maximized) {
                RECT win_rect{};
                GetWindowRect(hwnd, &win_rect);

                const int border_x = (std::max)(GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER), 6);
                const int border_y = (std::max)(GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER), 6);
                const int border_top = 6;

                const bool on_left = screen_x < win_rect.left + border_x;
                const bool on_right = screen_x >= win_rect.right - border_x;
                const bool on_top = screen_y < win_rect.top + border_top;
                const bool on_bottom = screen_y >= win_rect.bottom - border_y;

                if (on_top && on_left) {
                    return HTTOPLEFT;
                }
                if (on_top && on_right) {
                    return HTTOPRIGHT;
                }
                if (on_bottom && on_left) {
                    return HTBOTTOMLEFT;
                }
                if (on_bottom && on_right) {
                    return HTBOTTOMRIGHT;
                }
                if (on_top) {
                    return HTTOP;
                }
                if (on_bottom) {
                    return HTBOTTOM;
                }
                if (on_left) {
                    return HTLEFT;
                }
                if (on_right) {
                    return HTRIGHT;
                }
            }

            POINT pt{screen_x, screen_y};
            if (ScreenToClient(hwnd, &pt)) {
                if (self->is_in_drag_region(glm::vec2{static_cast<float>(pt.x), static_cast<float>(pt.y)})) {
                    return HTCAPTION;
                }
            }

            return HTCLIENT;
        }
    }

    return prev != nullptr ? CallWindowProcW(prev, hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

}
#endif

WindowSystem::~WindowSystem() {
    destroy();
}

bool WindowSystem::create(const WindowDesc& desc, SDL_Window* owner) {
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
    if (desc.style.borderless && desc.style.transparent) {
        SDL_SetHint("SDL_BORDERLESS_WINDOWED_STYLE", "0");
    } else if (desc.style.borderless) {
        SDL_SetHint("SDL_BORDERLESS_WINDOWED_STYLE", "1");
    }

    // An owned window is created hidden and shown once it has its owner: Windows decides on a taskbar entry when a
    // window is shown, and an owned window gets none.
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | window_style_flags(desc.style);
    if (owner != nullptr) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    window_ = SDL_CreateWindow(desc.title.c_str(), desc.size.x, desc.size.y, flags);
    if (window_ == nullptr) {
        return false;
    }
    transparent_ = desc.style.transparent;
    if (desc.position) {
        SDL_SetWindowPosition(window_, desc.position->x, desc.position->y);
    }
    if (owner != nullptr) {
        // Not SDL_PROP_WINDOW_CREATE_PARENT_POINTER: with SDL_WINDOW_UTILITY, SDL's Windows backend makes a hidden
        // window of its own the owner at creation and ignores the parent (WIN_CreateWindow). SDL_SetWindowParent
        // afterwards sets the real owner (GWLP_HWNDPARENT) and SDL still destroys its own one with the window. SDL
        // destroys an owned window before its owner (SDL_DestroyWindow); WindowManager closes owned windows first.
        if (!SDL_SetWindowParent(window_, owner)) {
            log::warn(std::string("Window: no owner for a window (") + SDL_GetError() + "); it opens unowned");
        }
        SDL_ShowWindow(window_);
    }
    // a drag-region click is handled manually (begin_drag_if_in_region()) instead of
    // being routed through the OS's own HTCAPTION/modal-loop drag, so nothing needs SDL to know
    // about drag_region_ at the hit-testing level at all. is_in_drag_region() exists as a plain,
    // directly-called geometry check.
#if defined(_WIN32)
    // Subclass on top of SDL's own WIN_WindowProc so:
    // 1. WM_NCHITTEST can resolve to HTTRANSPARENT for click-through, which SDL's own hit-test dispatch can never produce.
    // 2. Borderless windows (e.g. editor) get native edge/corner resize handles (HTLEFT/HTRIGHT/etc.) and custom drag region (HTCAPTION)
    //    for native Aero Snap, docking, and snap layouts.
    // 3. WM_NCCALCSIZE clamps maximized borderless windows to the monitor's work area (preventing taskbar overlap).
    // GWLP_USERDATA stores `this` pointer for the subclass wndproc.
    if (HWND hwnd = static_cast<HWND>(
                SDL_GetPointerProperty(SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
            hwnd != nullptr) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        win32_prev_wndproc_ = reinterpret_cast<void*>(
                SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&win32_hit_test_wndproc)));

        if (transparent_) {
            const LONG_PTR ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex_style | WS_EX_LAYERED);
            SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        } else if (desc.style.borderless) {
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
    }
#endif
    return true;
}

void WindowSystem::destroy() {
    transparent_ = false;
    click_through_applied_ = false;
    drag_region_.reset();
    drag_exclusions_.clear();
    // Releases capture (harmless if it was never held) rather than leaving a captured
    // mouse dangling past the window it was captured for.
    if (dragging_) {
        end_drag();
    }
#if defined(_WIN32)
    if (window_ != nullptr && win32_prev_wndproc_ != nullptr) {
        if (HWND hwnd = static_cast<HWND>(
                    SDL_GetPointerProperty(SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
                hwnd != nullptr) {
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(win32_prev_wndproc_));
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
    }
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

bool WindowSystem::is_presentable() const {
    if (window_ == nullptr) {
        return false;
    }
    constexpr SDL_WindowFlags kNotPresented = SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED;
    return (SDL_GetWindowFlags(window_) & kNotPresented) == 0;
}

float WindowSystem::refresh_rate() const {
    if (window_ == nullptr) {
        return 0.0f;
    }
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_));
    return mode != nullptr ? mode->refresh_rate : 0.0f;
}

void WindowSystem::set_title(std::string_view title) {
    if (window_ != nullptr) {
        const std::string text(title);
        SDL_SetWindowTitle(window_, text.c_str());
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

void WindowSystem::raise() {
    if (window_ == nullptr) {
        return;
    }
    if ((SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0) {
        SDL_RestoreWindow(window_);
    }
    SDL_RaiseWindow(window_);
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

void WindowSystem::minimize() {
    if (window_ != nullptr) {
        SDL_MinimizeWindow(window_);
    }
}

void WindowSystem::maximize() {
    if (window_ != nullptr) {
        SDL_MaximizeWindow(window_);
    }
}

void WindowSystem::restore() {
    if (window_ != nullptr) {
        SDL_RestoreWindow(window_);
    }
}

bool WindowSystem::is_maximized() const {
    return window_ != nullptr && (SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED) != 0;
}

bool WindowSystem::is_minimized() const {
    return window_ != nullptr && (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0;
}

bool WindowSystem::is_borderless() const noexcept {
    return window_ != nullptr && (SDL_GetWindowFlags(window_) & SDL_WINDOW_BORDERLESS) != 0;
}

bool WindowSystem::is_resizable() const noexcept {
    return window_ != nullptr && (SDL_GetWindowFlags(window_) & SDL_WINDOW_RESIZABLE) != 0;
}

void WindowSystem::show() {
    if (window_ != nullptr) {
        SDL_ShowWindow(window_);
    }
}

void WindowSystem::hide() {
    if (window_ != nullptr) {
        SDL_HideWindow(window_);
    }
}

bool WindowSystem::is_visible() const {
    return window_ != nullptr && (SDL_GetWindowFlags(window_) & SDL_WINDOW_HIDDEN) == 0;
}

void WindowSystem::start_text_input() {
    if (window_ == nullptr) {
        return;
    }
    // Single-line text. Do not set SDL_HINT_RETURN_KEY_HIDES_IME: that hint swallows Enter inside
    // SDL's Android JNI, so the field never sees a submit.
    const SDL_PropertiesID props = SDL_CreateProperties();
    if (props != 0) {
        SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_MULTILINE_BOOLEAN, false);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER, static_cast<Sint64>(SDL_TEXTINPUT_TYPE_TEXT));
    }
    SDL_StartTextInputWithProperties(window_, props);
    SDL_DestroyProperties(props);
}

void WindowSystem::set_text_input_area(render::Rect rect, int cursor) {
    if (window_ == nullptr) {
        return;
    }
    const SDL_Rect area{
            static_cast<int>(std::lround(rect.x)),
            static_cast<int>(std::lround(rect.y)),
            static_cast<int>(std::lround(rect.w)),
            static_cast<int>(std::lround(rect.h)),
    };
    SDL_SetTextInputArea(window_, &area, cursor);
}

void WindowSystem::stop_text_input() {
    if (window_ != nullptr) {
        SDL_ClearComposition(window_);
        SDL_StopTextInput(window_);
    }
}

bool WindowSystem::is_text_input_active() const {
    return window_ != nullptr && SDL_TextInputActive(window_);
}

void WindowSystem::update_click_through(bool pointer_hit_something) {
    if (window_ == nullptr) {
        return;
    }
    // the drag region must count as "hit" for this decision too, not just
    // MouseConsumed from UI hit-testing — otherwise WS_EX_TRANSPARENT stays applied for the whole
    // time the pointer sits in the drag region (a game's drag strip is not necessarily backed by an
    // actual UI widget that would set MouseConsumed on its own). That matters because real click
    // delivery for this layered+transparent window depends on whether WS_EX_TRANSPARENT is set on
    // the window at all at the moment of the real click, not on win32_hit_test_wndproc's
    // per-point HTCAPTION answer.
    bool effective_hit = pointer_hit_something;
    if (const std::optional<glm::vec2> cursor = cursor_client_position()) {
        if (is_in_drag_region(*cursor)) {
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

std::optional<glm::ivec2> WindowSystem::position() const {
    if (window_ == nullptr) {
        return std::nullopt;
    }
    int x = 0;
    int y = 0;
    if (!SDL_GetWindowPosition(window_, &x, &y)) {
        return std::nullopt;
    }
    return glm::ivec2{x, y};
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
    if (window_ == nullptr || !is_in_drag_region(window_local_pos)) {
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
    if (capture_ != nullptr) {
        capture_->begin_window_drag();
    }
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
    if (capture_ != nullptr) {
        capture_->end_window_drag();
    }
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
    if (style.maximized) {
        flags |= SDL_WINDOW_MAXIMIZED;
    }
    if (style.utility) {
        flags |= SDL_WINDOW_UTILITY;
    }
    return flags;
}

bool should_be_click_through(bool click_through_enabled, bool window_is_transparent, bool pointer_hit_something) noexcept {
    return click_through_enabled && window_is_transparent && !pointer_hit_something;
}

bool drag_region_contains(const std::optional<render::Rect>& region, std::span<const render::Rect> exclusions,
        glm::vec2 point) noexcept {
    const auto inside = [point](const render::Rect& rect) {
        return point.x >= rect.x && point.y >= rect.y && point.x < (rect.x + rect.w) && point.y < (rect.y + rect.h);
    };
    if (!region || !inside(*region)) {
        return false;
    }
    for (const render::Rect& hole : exclusions) {
        if (inside(hole)) {
            return false;
        }
    }
    return true;
}

bool WindowSystem::is_in_drag_region(glm::vec2 point) const noexcept {
    return drag_region_contains(drag_region_, drag_exclusions_, point);
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
