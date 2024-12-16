#include "window_system.h"

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace engine {

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

    const SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | window_style_flags(desc.style);
    window_ = SDL_CreateWindow(desc.title.c_str(), desc.size.x, desc.size.y, flags);
    if (window_ == nullptr) {
        return false;
    }
    transparent_ = desc.style.transparent;
    if (desc.position) {
        SDL_SetWindowPosition(window_, desc.position->x, desc.position->y);
    }
    // Installed unconditionally (SDD §21.7): a bordered window's OS titlebar already drags on its
    // own, and the callback is harmless there — it only ever returns SDL_HITTEST_DRAGGABLE inside
    // a region set via set_drag_region(), which stays nullopt unless the game opts in.
    SDL_SetWindowHitTest(window_, &window_drag_hit_test, this);
    return true;
}

void WindowSystem::destroy() {
    transparent_ = false;
    click_through_applied_ = false;
    drag_region_.reset();
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
    const bool click_through = should_be_click_through(click_through_enabled_, transparent_, pointer_hit_something);
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
    // Plain WS_EX_TRANSPARENT is enough here: this window's alpha compositing goes through DWM's
    // DwmEnableBlurBehindWindow (SDD §21.2, confirmed from SDL_windowswindow.c around
    // WIN_CreateWindow), not the legacy WS_EX_LAYERED path SDL_SetWindowOpacity uses elsewhere in
    // that same file — WS_EX_TRANSPARENT and WS_EX_LAYERED are independent bits, and hit-test
    // pass-through only needs the former.
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
