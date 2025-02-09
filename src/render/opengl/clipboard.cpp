#include "clipboard.h"

#include "gl_includes.h"

namespace engine {

void sdl_clipboard_set_text(std::string_view text) {
    SDL_SetClipboardText(std::string(text).c_str());
}

std::optional<std::string> sdl_clipboard_get_text() {
    if (!SDL_HasClipboardText()) {
        return std::nullopt;
    }
    char* text = SDL_GetClipboardText();
    if (text == nullptr) {
        return std::nullopt;
    }
    std::string result(text);
    SDL_free(text);
    return result;
}

}
