#include "window_system.h"

namespace engine {

WindowSystem::~WindowSystem() {
    destroy();
}

bool WindowSystem::create(std::string_view title, glm::ivec2 size) {
    destroy();

#if defined(__EMSCRIPTEN__)
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

}
