#include <engine/core/sdl_fatal_error.h>

#include "render/opengl/gl_includes.h"

#include <string>

namespace engine {

void SdlFatalError::attach(ApplicationState& app, void* native_window) {
    app_ = &app;
    native_window_ = native_window;
}

void SdlFatalError::report(std::string_view message) {
    const std::string text(message);
    SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, "Engine", text.c_str(), static_cast<SDL_Window*>(native_window_));
    if (app_ != nullptr) {
        app_->Quit();
    }
}

}
