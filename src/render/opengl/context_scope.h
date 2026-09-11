#pragma once

#include <SDL3/SDL_video.h>

#include <functional>

namespace engine::render {

// Makes the engine's GL context current for one scope and puts the caller's context back afterwards. An empty
// `make_current` does nothing.
class ContextScope {
public:
    explicit ContextScope(const std::function<void()>& make_current) {
        if (!make_current) {
            return;
        }
        prev_window_ = SDL_GL_GetCurrentWindow();
        prev_context_ = SDL_GL_GetCurrentContext();
        make_current();
        const SDL_GLContext current_context = SDL_GL_GetCurrentContext();
        needs_restore_ = (prev_context_ != nullptr && prev_context_ != current_context && prev_window_ != nullptr);
    }

    ~ContextScope() {
        if (needs_restore_) {
            SDL_GL_MakeCurrent(prev_window_, prev_context_);
        }
    }

    ContextScope(const ContextScope&) = delete;
    ContextScope& operator=(const ContextScope&) = delete;

private:
    SDL_Window* prev_window_ = nullptr;
    SDL_GLContext prev_context_ = nullptr;
    bool needs_restore_ = false;
};

}
