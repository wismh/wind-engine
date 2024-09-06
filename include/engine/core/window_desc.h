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
};

struct WindowDesc {
    std::string title = "Game";
    glm::ivec2 size{800, 600};
    std::optional<glm::ivec2> position;   // nullopt = platform default placement
    WindowStyle style;
};

}
