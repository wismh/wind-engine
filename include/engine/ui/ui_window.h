#pragma once

// docs/tech/features/Windowing.md

#include <engine/core/window_control.h>
#include <engine/core/window_desc.h>
#include <engine/core/worlds.h>
#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>
#include <engine/render/commands.h>
#include <engine/ui/canvas.h>

#include <glm/vec2.hpp>

#include <expected>
#include <memory>
#include <optional>
#include <string_view>

namespace engine {

enum class UiWindowError {
    // `IWindowControl::open_window` answered nullopt: no primary window yet, or a `WindowDesc::owner` that is not open.
    WindowRefused,
};

// The enumerator name, for logs and tests.
[[nodiscard]] constexpr std::string_view to_string(UiWindowError error) noexcept {
    switch (error) {
        case UiWindowError::WindowRefused:
            return "WindowRefused";
    }
    return "Unknown";
}

// Where a window of `size` sits when centred on `area` (a display's usable area, screen pixels). Integer pixels,
// rounded down: a 3 px difference puts the window 1 px in.
[[nodiscard]] glm::ivec2 centered_position(render::Rect area, glm::ivec2 size) noexcept;

struct UiWindowState;

// A secondary window that shows one UI canvas of a world, owned by a value: open it, keep it, and its destructor
// closes everything in the right order. It replaces the sequence a game wrote by hand for every window:
//
//   open: IWindowControl::open_window, Worlds::bind_window, Worlds::enable_ui, spawn the UiCanvas on that window
//   close: destroy the window's canvases, Worlds::unbind_window, IWindowControl::close_window
//
//   auto settings = engine::UiWindow::open(services.windows, services.worlds, world(), desc, canvas);
//   if (!settings) { /* settings.error() */ }
//   settings->center_on_work_area();
//   settings->set_drag_region(engine::render::Rect{0, 0, 480, 32});    // a title bar; its buttons stay clickable
//   ...
//   if (settings->take_close_requested()) { settings->close(); }
//
// Move-only. `close()` and the destructor are safe to repeat and safe after the window is already gone: the owner
// window closing (`WindowDesc::owner`) closes an owned window on the platform side, and the UiWindow then only
// finishes the rest. A UiWindow must be destroyed before the world and the services it was opened with. If its world
// went first (`Worlds::destroy`), it skips the world and just closes the window. Main thread only, like every
// engine API.
class UiWindow {
public:
    // Opens the window, binds it to `world` (which must come from `worlds.add()`), turns on UI for the world, and
    // spawns `canvas` on the new window (its `window` field is set for you).
    [[nodiscard]] static std::expected<UiWindow, UiWindowError> open(IWindowControl& windows, Worlds& worlds,
            ecs::World& world, const WindowDesc& desc, ui::UiCanvas canvas);

    UiWindow(UiWindow&& other) noexcept;
    UiWindow& operator=(UiWindow&& other) noexcept;
    UiWindow(const UiWindow&) = delete;
    UiWindow& operator=(const UiWindow&) = delete;
    ~UiWindow();

    // The window's id. A UiWindow that was moved from has none (kPrimaryWindow's value, which is never a UiWindow).
    [[nodiscard]] WindowId id() const noexcept;
    // The canvas `open` spawned. Other canvases on this window are the game's to spawn (set `UiCanvas::window` to
    // `id()`); closing destroys every canvas of the window, not only this one.
    [[nodiscard]] ecs::Entity canvas() const noexcept;
    // True while the window is still open: false after `close()`, after the platform closed it (its owner closed),
    // and after a move.
    [[nodiscard]] bool is_open() const;

    // True once per close request of this window (the OS close button, `WindowCloseRequestedEvent`). The engine does
    // not close anything for you; a game that wants it closed calls `close()` or lets the destructor run. Two windows
    // each get only their own requests: no `if` over `WindowId`.
    [[nodiscard]] bool take_close_requested();

    // Destroys the window's canvases, unbinds it from its world, and closes it, in that order. Safe to repeat.
    void close();

    // Makes `region` (window client pixels) the part of the window that drags it, for a borderless window. The
    // interactive UI inside the region (buttons, text fields, sliders, scroll areas, open popups) is excluded
    // automatically and stays clickable, and follows the layout as it changes: no rect to notch by hand. nullopt
    // clears it. Takes effect from the next frame's layout.
    void set_drag_region(std::optional<render::Rect> region);

    // Moves the window so it sits in the middle of the usable area (display minus taskbar and menu bar) of the
    // display it is on. Does nothing where the platform cannot say.
    void center_on_work_area();

private:
    explicit UiWindow(std::unique_ptr<UiWindowState> state) noexcept;

    std::unique_ptr<UiWindowState> state_;
};

}
