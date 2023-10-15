#pragma once

#include "opengl_backend.h"
#include "opengl_canvas.h"
#include "opengl_factory.h"
#include "window_system.h"

#include <engine/igame.h>
#include <engine/render/command_buffer.h>

#include <memory>
#include <string_view>

namespace engine::render {

struct OpenGLRuntime {
    WindowSystem window;
    CommandBuffer commands;
    OpenGLFactory factory;
    OpenGLRenderBackend backend;
    std::unique_ptr<OpenGLCanvas> canvas;

    [[nodiscard]] bool init(std::string_view title, glm::ivec2 size) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        if (!window.create(title, size)) {
            return false;
        }
        canvas = std::make_unique<OpenGLCanvas>(window, commands, backend);
        return canvas->init();
    }
};

[[nodiscard]] inline bool init_video() {
    return SDL_InitSubSystem(SDL_INIT_VIDEO);
}

inline void quit_video() {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

[[nodiscard]] inline bool create_runtime_for_game(IGame& game, OpenGLRuntime& runtime) {
    return runtime.init(game.WindowTitle(), game.WindowSize());
}

}
