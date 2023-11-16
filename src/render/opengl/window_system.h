#pragma once

#include "gl_includes.h"

#include <glm/vec2.hpp>

#include <string>
#include <string_view>

namespace engine {

class WindowSystem {
public:
    WindowSystem() = default;
    ~WindowSystem();

    WindowSystem(const WindowSystem&) = delete;
    WindowSystem& operator=(const WindowSystem&) = delete;

    [[nodiscard]] bool create(std::string_view title, glm::ivec2 size);
    void destroy();
    void swap() const;

    [[nodiscard]] SDL_Window* window() const noexcept {
        return window_;
    }

    [[nodiscard]] glm::ivec2 size() const;
    [[nodiscard]] glm::ivec2 drawable_size() const;

private:
    SDL_Window* window_ = nullptr;
};

}
