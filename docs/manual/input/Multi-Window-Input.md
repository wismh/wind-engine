# Multi-Window Input

Wind supports desktop multi-window applications, such as editor tool panels, secondary viewports, and multi-monitor setups.

---

## 1. Opening Secondary Windows

Open additional windows using `services.windows` (`engine::IWindowControl`):

```cpp
#include <engine/core/window_control.h>

engine::WindowDesc tool_desc;
tool_desc.title = "Tools & Inspector";
tool_desc.size = {400, 600};
tool_desc.resizable = true;

std::expected<engine::WindowId, engine::WindowError> res =
    services.windows.open_window(tool_desc);

if (res) {
    engine::WindowId tool_window_id = *res;
}
```

---

## 2. Window Routing for Input

By default, input events are tagged with their originating `WindowId`:

```cpp
struct MouseEvent {
    WindowId window; // Identifies which window received the event
    // ...
};
```

You can route events from different windows to specific `World` instances using `InputSystem::set_router`:

```cpp
services.input.set_router([this, tool_window_id](engine::WindowId win) -> engine::ecs::World* {
    if (win == tool_window_id) {
        return &tool_world_;
    }
    return &world(); // Primary game world
});
```

---

## 3. Overlay Windows & Click-Through

To create non-interactive HUD overlays or transparent informational viewports:

```cpp
services.windows.set_overlay_mode(tool_window_id, engine::OverlayMode::TransparentClickThrough);
```

In click-through mode, mouse events pass straight through the window into background applications or underlying windows.

---

## Next Steps

- Integrate audio effects in [Sound & Music](../audio-and-haptics/Sound-and-Music.md).
- Cook and load game assets in [Asset Pipeline](../assets-and-loc/Asset-Pipeline.md).
