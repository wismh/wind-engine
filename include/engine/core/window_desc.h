#pragma once

// docs/tech/features/Windowing.md

#include <glm/vec2.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace engine {

// Identifies one live OS window. enum class gets std::hash and == for free since
// C++14 (LWG 2148), so this works as an unordered_map key with no extra machinery.
enum class WindowId : std::uint32_t {};
inline constexpr WindowId kPrimaryWindow{0};

// Create-time-only flags: transparent in particular can't be toggled on an existing
// SDL window, so a game that wants to switch from an opaque window to a transparent one opens a
// second window rather than mutating this struct after create().
struct WindowStyle {
    bool borderless = false;
    bool always_on_top = false;
    bool transparent = false;
    // false omits SDL_WINDOW_RESIZABLE at creation. On Windows this also removes WS_MAXIMIZEBOX
    // (SDL's GetWindowStyle() only adds it for a resizable window — SDL_windowswindow.c), preventing
    // the window from being maximized (e.g. by OS shortcuts or double-clicking).
    bool resizable = true;
    // Opens maximized. The title bar and taskbar stay; this is not fullscreen. `size` is the
    // restored size. SDL_MaximizeWindow refuses the request unless SDL_WINDOW_RESIZABLE is also
    // set, so maximized without resizable leaves the window at `size`.
    bool maximized = false;
    // A tool window: no taskbar entry and not in the window switcher (Windows: WS_EX_TOOLWINDOW, a smaller title
    // bar). Create only. Pair it with WindowDesc::owner so the window stays reachable above its owner.
    bool utility = false;
};

struct WindowDesc {
    std::string title = "Game";
    glm::ivec2 size{800, 600};
    std::optional<glm::ivec2> position;   // nullopt = platform default placement
    WindowStyle style;
    // The open window this one belongs to: it stays above its owner, hides and minimizes with it, and closes when
    // the owner closes (close_window of the owner closes it first). nullopt: a top-level window of its own. An owner
    // that is not open makes open_window fail. The primary window cannot have one (nothing is open before it).
    // Create only. On a platform without owned windows the window opens unowned.
    std::optional<WindowId> owner;
};

}
