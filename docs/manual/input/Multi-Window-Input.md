# Multi-Window Input

Wind supports desktop multi-window applications, such as tool panels, secondary viewports, and multi-monitor setups.

---

## 1. Opening Secondary Windows

Open additional windows using `services.windows` (`engine::IWindowControl`). Window flags are in `WindowDesc::style`:

```cpp
#include <engine/core/window_control.h>

engine::WindowDesc tool_desc;
tool_desc.title = "Tools & Inspector";
tool_desc.size = {400, 600};
tool_desc.style.resizable = true;
tool_desc.style.utility = true;                // a tool window: no taskbar entry
tool_desc.owner = engine::kPrimaryWindow;      // stays above the main window and closes with it

std::optional<engine::WindowId> opened = services_.windows.open_window(tool_desc);

if (opened) {
    tool_window_ = *opened;
}
```

`open_window` returns `std::nullopt` when it cannot create the window (for example when `owner` is not open). `WindowStyle` has `borderless`, `always_on_top`, `transparent`, `resizable`, `maximized`, and `utility`; `WindowDesc` also has `position` and `owner`. `transparent`, `utility`, `maximized`, and `owner` apply only when the window is created. `set_borderless`, `set_always_on_top`, `set_title`, `set_position`, and `resize` change an open window.

`close_window(id)` closes a window and the windows it owns. The OS close button does not close anything by itself: the engine sends `engine::ui::WindowCloseRequestedEvent` (`window`), and your game decides ([Game Lifecycle](../architecture/Game-Lifecycle.md#4-lifecycle-hooks)).

### `UiWindow`: a window that cleans up after itself

A window that shows a UI canvas needs several steps to open (`open_window`, bind it to a world, turn on UI for the world, spawn the canvas) and the same steps in reverse to close, in the right order. `engine::UiWindow` (`<engine/ui/ui_window.h>`) does both and owns the result:

```cpp
#include <engine/ui/ui_window.h>

class SettingsPanel {
public:
    explicit SettingsPanel(const engine::EngineServices& services, engine::ecs::World& world) {
        auto opened = engine::UiWindow::open(services.windows, services.worlds, world,
                engine::WindowDesc{.title = "Settings", .size = {480, 360}, .style = {.borderless = true}},
                engine::ui::UiCanvas{.document = assets::ui::settings, .stylesheet = assets::css::settings,
                                     .data_context = vm_, .fit = engine::ui::UiFit::FillWindow});
        if (!opened) {
            engine::log::warn("Settings window: " + std::string(engine::to_string(opened.error())));
            return;
        }
        window_.emplace(std::move(*opened));
        window_->center_on_work_area();
        window_->set_drag_region(engine::render::Rect{0, 0, 480, 32});   // the title bar
    }

    void frame() {
        if (window_ && window_->take_close_requested()) {
            window_.reset();                       // canvases, unbind, close, in that order
        }
    }

private:
    std::shared_ptr<SettingsViewModel> vm_ = std::make_shared<SettingsViewModel>();
    std::optional<engine::UiWindow> window_;      // declared last: destroyed before what it was opened with
};
```

- `open` returns the window or an `UiWindowError` (`WindowRefused`: no primary window yet, or an `owner` that is not open). It sets the canvas's `window` for you.
- The destructor, or `close()` (safe to repeat), destroys every canvas on the window, unbinds it from the world, then closes it. `UiWindow` is move-only. A moved-from one does nothing.
- `take_close_requested()` is true once per OS close request of **this** window. Two `UiWindow`s each see only their own, so there is no `if (event.window == ...)` chain. The engine never closes the window for you.
- If the platform closes the window first (its `owner` closed), `is_open()` turns false, and the destructor only finishes the rest: no second close.
- `set_drag_region(rect)` makes `rect` drag a borderless window, and **leaves out every interactive element inside it automatically**: buttons, text fields, sliders, scroll areas, and open popups stay clickable, and the holes follow the layout (they are refreshed whenever the last layout changed them). The first frame after opening has no layout yet, so the whole region drags until the next one. You do not notch the rectangle by hand (the problem the note in section 3 describes).
- `center_on_work_area()` puts the window in the middle of the usable area of its display (no taskbar or menu bar). Where the platform cannot say, it does nothing.
- Destroy a `UiWindow` before the world and the services it was opened with (declare it after them). If the world went first, the destructor skips the world and only closes the window.

---

## 2. Window Routing for Input

Every input event is tagged with the window it came from (`MouseEvent::window`, `KeyEvent::window`, `TextInputEvent::window`). The engine delivers it to the world the window is **bound** to. A window that no world owns drops its events, and nothing is drawn to it.

The game's primary window is bound for you. A window you open is not, so bind it. The simplest case is one world drawing into both windows:

```cpp
services_.worlds.bind_window(tool_window_, world());
```

The world's sprites and meshes are then drawn into the new window as well (with that window's size for the projection), and a `UiCanvas` with `.window = tool_window_` is laid out there and gets that window's pointer events. Window sizes and the pointer live on the process-wide presentation, keyed by `WindowId`: `engine::ui::window_size_for(world, id)` gives the size.

For an independent simulation, give the window a world of its own:

```cpp
engine::ecs::World& tool_world = services_.worlds.add();
services_.worlds.bind_window(tool_window_, tool_world);
services_.worlds.enable_ui(tool_world);   // let this world draw UI
```

One window belongs to exactly one world; binding it to a second world is a fatal error. Before you close a window, unbind it: `services_.worlds.unbind_window(id)`, then `services_.windows.close_window(id)`. `Worlds::destroy(world)` unbinds all of its windows.

Do not replace `InputSystem::set_router`: the engine has already set it to look up the world a window is bound to.

---

## 3. Overlay Windows & Click-Through

To create non-interactive HUD overlays or transparent informational viewports, open a window with `style.transparent = true` (create-time only) and turn click-through on for it:

```cpp
services_.windows.set_click_through_enabled(true, overlay_window_);
```

While click-through is on, the window lets mouse events pass to the windows behind it wherever the UI under the cursor did not consume the mouse (`presentation.mouse`), so interactive buttons on the overlay stay clickable. This is a bounding-box decision, not a per-pixel test of the framebuffer.

`services_.windows.set_overlay_mode(engine::OverlayMode::...)` is engine-wide, not per window. `Auto` (the default) runs the overlay hooks (cursor polling, per-window hit-test sync) as soon as any open window is transparent; `AlwaysDisabled` keeps them off, `AlwaysEnabled` runs them before a transparent window exists. Set it before opening the first transparent window.

While click-through is on, a world entity with a `PointerTarget` under the cursor also keeps the click on the window (the engine adds the window to `presentation.mouse` for it), so a transparent overlay needs no hit-testing code for its sprites ([Raw Input & Mouse](Raw-Input-and-Mouse.md#3-clickable-sprites-with-pointertarget)).

A borderless window has no title bar to drag. `services_.windows.set_drag_region(rect, window)` marks a region (window pixels) as a drag handle. A left click inside it starts a window drag and never reaches the UI, so a button inside that rectangle cannot be clicked. Either leave `set_drag_exclusions(rects, window)` the rectangles of the controls (clicks there reach the UI), or open the window as a [`UiWindow`](#uiwindow-a-window-that-cleans-up-after-itself) and let `UiWindow::set_drag_region` compute them from the layout.

---

## 4. The Global Cursor and Mouse Capture

To follow the cursor across windows, or outside all of them (dragging a panel out of a window, a tooltip that tracks the mouse over the desktop), ask the window control. No platform header is needed:

```cpp
#include <engine/core/window_control.h>

// Screen coordinates, the same space as windows.position(window).
if (const auto cursor = windows.cursor_screen_position()) {
    const auto client = engine::cursor_client_position(windows, secondary_window);   // vs the window's top-left
}

const bool left_held = windows.is_mouse_button_down(engine::MouseButton::Left);   // the OS state, anywhere

// A drag that starts in a window keeps receiving events after the cursor leaves it:
windows.capture_mouse(window, true);     // on button down
windows.capture_mouse(window, false);    // on button up: always release it
```

`cursor_screen_position()` is `nullopt` and `is_mouse_button_down` is false where the platform cannot say: Wayland, the browser, and Android. `capture_mouse` returns whether the capture is now on and is false on those platforms (nothing is held, nothing fails). A window that is not open cannot capture. Check the return value, and use the events you already get when it is false.

One window holds the capture at a time, and closing that window releases it. The capture does not conflict with the engine's own: moving a borderless window by its drag region captures too, and neither switches the other off. A capture left on after the button went up keeps other applications from getting clicks, so release it on button up.

---

## Next Steps

- Integrate audio effects in [Sound & Music](../audio-and-haptics/Sound-and-Music.md).
- Cook and load game assets in [Asset Pipeline](../assets-and-loc/Asset-Pipeline.md).
