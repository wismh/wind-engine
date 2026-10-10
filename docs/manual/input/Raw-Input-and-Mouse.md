# Raw Input & Mouse

While gameplay keys are bound through `ActionId`, specific interactions—such as continuous mouse movement, dragging, and held-key auto-repeat—stay on raw hardware events.

---

## 1. Pointer Coordinates and `MouseEvent`

Mouse positions, wheel deltas, and dragging are processed from `engine::MouseEvent`. Read it from the world's queue with a cursor, like any event ([Events](../ecs/Events.md)):

```cpp
#include <engine/core/input_system.h>
#include <engine/ecs/events.h>

void mouse_tracker_system(engine::ecs::World& world) {
    using engine::ecs::EventCursor;
    using engine::ecs::EventReader;

    for (const engine::MouseEvent& m :
            EventReader<engine::MouseEvent>{world, world.ctx<EventCursor<engine::MouseEvent>>()}) {
        if (m.kind == engine::MouseEvent::Kind::Move) {
            glm::vec2 mouse_pos = m.position;   // window pixel coordinates
            glm::vec2 delta = m.relative;       // movement since the previous event
        } else if (m.kind == engine::MouseEvent::Kind::Wheel) {
            float zoom_delta = m.wheel_y;
        } else if (m.kind == engine::MouseEvent::Kind::Down && m.button == engine::MouseButton::Left) {
            // m.clicks is 1 for a single click, 2 for a double click
        }
    }
}
```

`MouseEvent::window` says which window the event came from. To turn `position` into world units, use `screen_to_world` ([Camera](../rendering/Camera.md#2-coordinate-conversions)).

---

## 2. Preventing Click-Through

When the player clicks on a UI button or drags a UI scrollbar, that click must not also trigger gameplay (firing a weapon, selecting a unit). The UI records which windows it consumed the mouse in on the process-wide `Presentation`. Read it from a system on `Schedule::Frame`, `Phase::Game`, which runs after the engine's UI input phase:

```cpp
#include <engine/ui/presentation.h>

void gameplay_click_system(engine::ecs::World& world) {
    // Did the UI take the mouse in this window this frame?
    const engine::ui::Presentation& presentation = engine::ui::presentation_of(world);
    if (presentation.mouse.consumed_for(engine::kPrimaryWindow)) {
        // UI was hit! Do NOT trigger gameplay click
        return;
    }

    // Process gameplay selection or unit movement...
}
```

The set is cleared once at the start of every frame and filled again while a mouse Move, Down, or Wheel event lands on an interactive UI element (a button, a text field, a scrollable area, an open popup). A plain label or empty panel does not consume the mouse. `world.ctx<engine::ui::MouseConsumed>()` is not that set: it never sees the UI's hits.

---

## 3. Key Repeat Handling (`KeyEvent`)

`InputSystem` suppresses OS key auto-repeat for `InputEvent` (so holding Space only fires one Jump action).

If your gameplay mechanic requires continuous repeating steps while a key is held (e.g. stepping a simulation forward frame-by-frame with Right Arrow):
1. Do not bind that key as an `ActionId`.
2. Read the raw `engine::KeyEvent` directly; it also carries the repeats (`repeat == true`):

```cpp
for (const engine::KeyEvent& k :
        engine::ecs::EventReader<engine::KeyEvent>{world, world.ctx<engine::ecs::EventCursor<engine::KeyEvent>>()}) {
    if (k.key == engine::KeyCode::Right && k.down) {   // down is true for the first press and for repeats
        advance_simulation_single_step();
    }
}
```

`KeyEvent` is `{window, key, down, repeat}`. A held key sends `down = true` once with `repeat = false`, then more events with `repeat = true`.

---

## Next Steps

- Manage secondary windows with [Multi-Window Input](Multi-Window-Input.md).
- Trigger sound effects in [Sound & Music](../audio-and-haptics/Sound-and-Music.md).
