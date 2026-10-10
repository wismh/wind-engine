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

A borderless window has no title bar to drag. `services_.windows.set_drag_region(rect, window)` marks a region (window pixels) as a drag handle. A left click inside it starts a window drag and never reaches the UI, so a button inside that rectangle cannot be clicked: keep the rectangle clear of interactive controls.

---

## Next Steps

- Integrate audio effects in [Sound & Music](../audio-and-haptics/Sound-and-Music.md).
- Cook and load game assets in [Asset Pipeline](../assets-and-loc/Asset-Pipeline.md).
