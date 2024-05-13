#include "window_system.h"

#include <cstddef>
#include <cstdint>

namespace engine {

WindowSystem::~WindowSystem() {
    destroy();
}

bool WindowSystem::create(std::string_view title, glm::ivec2 size) {
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

    const std::string title_z(title);
    window_ = SDL_CreateWindow(title_z.c_str(), size.x, size.y, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    return window_ != nullptr;
}

void WindowSystem::destroy() {
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
