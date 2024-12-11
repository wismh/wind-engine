#pragma once

#include "gl_includes.h"

#include <engine/render/graphic_factory.h>

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
    void set_icon(const render::TextureDesc& desc);

    [[nodiscard]] SDL_Window* window() const noexcept {
        return window_;
    }

    [[nodiscard]] glm::ivec2 size() const;
    [[nodiscard]] glm::ivec2 drawable_size() const;

private:
    SDL_Window* window_ = nullptr;
};

// Split out from set_icon() so the TextureDesc -> SDL_Surface byte layout is unit-testable
// without SDL_Init(SDL_INIT_VIDEO) or a real window. Caller owns the returned surface.
[[nodiscard]] SDL_Surface* make_icon_surface(const render::TextureDesc& desc);

}
