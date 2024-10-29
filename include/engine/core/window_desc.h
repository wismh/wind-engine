#pragma once

#include <glm/vec2.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace engine {

// Identifies one live OS window (SDD §21.5). enum class gets std::hash and == for free since
// C++14 (LWG 2148), so this works as an unordered_map key with no extra machinery.
enum class WindowId : std::uint32_t {};
inline constexpr WindowId kPrimaryWindow{0};

// Create-time-only flags (SDD §21.2): transparent in particular can't be toggled on an existing
// SDL window, so a game that wants to switch from an opaque window to a transparent one opens a
// second window rather than mutating this struct after create().
struct WindowStyle {
    bool borderless = false;
    bool always_on_top = false;
    bool transparent = false;
    // false omits SDL_WINDOW_RESIZABLE at creation. On Windows this also removes WS_MAXIMIZEBOX
    // (SDL's GetWindowStyle() only adds it for a resizable window — SDL_windowswindow.c), which is
    // what stops a double-click inside a set_drag_region() (window_control.h) from maximizing a
    // fixed-size overlay: without a titlebar, SDL reports HTCAPTION for a drag region, and Windows
    // treats a double-click on HTCAPTION as SC_MAXIMIZE whenever WS_MAXIMIZEBOX is present.
    bool resizable = true;
};

struct WindowDesc {
    std::string title = "Game";
    glm::ivec2 size{800, 600};
    std::optional<glm::ivec2> position;   // nullopt = platform default placement
    WindowStyle style;
};

}
