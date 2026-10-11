#include "mouse_capture.h"

#include "gl_includes.h"

namespace engine {

namespace {

bool sdl_apply(bool on) {
    return SDL_CaptureMouse(on);
}

}

MouseCapture::MouseCapture(Apply apply) noexcept
    : apply_(apply != nullptr ? apply : &sdl_apply) {}

void MouseCapture::begin_window_drag() {
    ++window_drags_;
    sync();
}

void MouseCapture::end_window_drag() {
    if (window_drags_ > 0) {
        --window_drags_;
    }
    sync();
}

bool MouseCapture::capture(WindowId window, bool on) {
    if (!on) {
        if (!game_owner_ || *game_owner_ != window) {
            return false;
        }
        game_owner_.reset();
        sync();
        return false;
    }
    const std::optional<WindowId> previous = game_owner_;
    game_owner_ = window;
    sync();
    if (!applied_) {
        game_owner_ = previous;
        sync();
        return false;
    }
    return true;
}

bool MouseCapture::captured_by(WindowId window) const noexcept {
    return game_owner_ && *game_owner_ == window;
}

void MouseCapture::window_closed(WindowId window) {
    if (game_owner_ && *game_owner_ == window) {
        game_owner_.reset();
        sync();
    }
}

void MouseCapture::sync() {
    const bool wanted = window_drags_ > 0 || game_owner_.has_value();
    if (wanted == applied_) {
        return;
    }
    // Turning it on can be refused (no focused window, a platform without capture). Turning it off is what the
    // caller wants whatever SDL answers: a release that reports failure must not leave the state saying "captured".
    const bool done = apply_(wanted);
    applied_ = wanted && done;
}

bool global_mouse_supported(std::string_view video_driver) noexcept {
    if (video_driver.empty()) {
        return false;
    }
    return video_driver != "wayland" && video_driver != "emscripten" && video_driver != "android";
}

namespace {

bool global_state(float& x, float& y, SDL_MouseButtonFlags& buttons) {
    const char* const driver = SDL_GetCurrentVideoDriver();
    if (!global_mouse_supported(driver != nullptr ? driver : "")) {
        return false;
    }
    buttons = SDL_GetGlobalMouseState(&x, &y);
    return true;
}

}

std::optional<glm::vec2> global_cursor_position() {
    float x = 0.0f;
    float y = 0.0f;
    SDL_MouseButtonFlags buttons = 0;
    if (!global_state(x, y, buttons)) {
        return std::nullopt;
    }
    return glm::vec2{x, y};
}

bool global_mouse_button_down(MouseButton button) {
    float x = 0.0f;
    float y = 0.0f;
    SDL_MouseButtonFlags buttons = 0;
    if (!global_state(x, y, buttons)) {
        return false;
    }
    switch (button) {
        case MouseButton::Left:
            return (buttons & SDL_BUTTON_LMASK) != 0;
        case MouseButton::Middle:
            return (buttons & SDL_BUTTON_MMASK) != 0;
        case MouseButton::Right:
            return (buttons & SDL_BUTTON_RMASK) != 0;
        case MouseButton::None:
            break;
    }
    return false;
}

}
