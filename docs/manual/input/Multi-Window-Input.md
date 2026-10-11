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

A borderless window has no title bar to drag. `services_.windows.set_drag_region(rect, window)` marks a region (window pixels) as a drag handle. A left click inside it starts a window drag and never reaches the UI, so a button inside that rectangle cannot be clicked: keep the rectangle clear of interactive controls.

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
