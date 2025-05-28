# Raw Input & Mouse

While gameplay keys are bound through `ActionId`, specific interactions—such as continuous mouse movement, dragging, and held-key auto-repeat—stay on raw hardware events.

---

## 1. Pointer Coordinates and `MouseEvent`

Mouse positions, wheel deltas, and dragging are processed from `engine::MouseEvent`:

```cpp
#include <engine/core/input_system.h>
#include <engine/ecs/events.h>

void mouse_tracker_system(engine::ecs::World& world) {
    auto& events = world.ctx<engine::ecs::Events<engine::MouseEvent>>();

    for (const auto& m : reader.read(events)) {
        if (m.kind == engine::MouseEvent::Kind::Move) {
            glm::vec2 mouse_pos = m.position; // Screen pixel coordinates
            glm::vec2 delta = m.relative;      // Movement offset since last frame
        } else if (m.kind == engine::MouseEvent::Kind::Wheel) {
            float zoom_delta = m.wheel_y;
        }
    }
}
```

---

## 2. Preventing Click-Through: `MouseConsumed`

When the player clicks on a UI button or drags a UI scrollbar, that click must not trigger gameplay actions (such as firing a weapon or selecting a unit):

```cpp
#include <engine/ui/presentation.h>

void gameplay_click_system(engine::ecs::World& world) {
    // Check if UI consumed the mouse in this frame
    const auto& consumed = world.ctx<engine::ui::MouseConsumed>();
    if (consumed.consumed_windows.contains(engine::kPrimaryWindow)) {
        // UI was clicked! Do NOT trigger gameplay click
        return;
    }

    // Process gameplay selection or unit movement...
}
```

`MouseConsumed` is cleared at the start of each frame and latched whenever a mouse Move, Down, or Wheel event hits an active UI element.

---

## 3. Key Repeat Handling (`KeyEvent`)

`InputSystem` suppresses OS key auto-repeat for `InputEvent` (so holding Space only fires one Jump action).

If your gameplay mechanic requires continuous repeating steps while a key is held (e.g. stepping a simulation forward frame-by-frame with Right Arrow):
1. Do not bind that key as an `ActionId`.
2. Read the raw `engine::KeyEvent` directly and check `repeat == true`:

```cpp
auto& key_events = world.ctx<engine::ecs::Events<engine::KeyEvent>>();
for (const auto& k : reader.read(key_events)) {
    if (k.key == engine::KeyCode::Right && (k.down || k.repeat)) {
        advance_simulation_single_step();
    }
}
```

---

## Next Steps

- Manage secondary windows with [Multi-Window Input](Multi-Window-Input.md).
- Trigger sound effects in [Sound & Music](../audio-and-haptics/Sound-and-Music.md).
