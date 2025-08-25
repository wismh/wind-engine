#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace engine {

// SDL's clipboard is process-global, not per-SDL_Window*, so these are free functions rather than
// WindowSystem methods. EngineRuntime wires them into ui::UiClipboard (include/engine/ui/canvas.h)
// once at startup; ui/canvas.cpp itself never sees SDL.
void sdl_clipboard_set_text(std::string_view text);
[[nodiscard]] std::optional<std::string> sdl_clipboard_get_text();

}
