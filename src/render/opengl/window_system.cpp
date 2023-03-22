#include "window_system.h"

namespace engine {

WindowSystem::~WindowSystem() {
    destroy();
}

bool WindowSystem::create(std::string_view title, glm::ivec2 size) {
    destroy();

    const std::string title_z(title);
    window_ = SDL_CreateWindow(title_z.c_str(), size.x, size.y, SDL_WINDOW_OPENGL);
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

}
