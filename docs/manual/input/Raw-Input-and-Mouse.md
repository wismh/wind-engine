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

The set is cleared once at the start of every frame and filled again while a mouse Move, Down, or Wheel event lands on an interactive UI element (a button, a text field, a scrollable area, an open popup). A plain label or empty panel does not consume the mouse. `world.ctx<engine::ui::MouseConsumed>()` is not that set: it never sees the UI's hits. For sprites and other world entities use [`PointerTarget`](#3-clickable-sprites-with-pointertarget) instead of testing the cursor yourself.

---

## 3. Clickable Sprites with `PointerTarget`

For things in the world (a unit, a coin, a world-space button), give the entity a `PointerTarget` instead of hit-testing by hand. The engine tests the cursor against it, after the UI has had its turn, and counts it for click-through itself: you never write into `Presentation.mouse` for a sprite.

```cpp
#include <engine/ecs/pointer_target.h>

// A 2 x 1 unit rectangle around the entity's position. With a Sprite that has a pixel_size,
// leave size at {0, 0} and the sprite's own size and pivot are used.
world.emplace<engine::PointerTarget>(coin, engine::PointerTarget{.size = {2.0f, 1.0f}});

void coin_system(engine::ecs::World& world) {
    for (const engine::PointerTargetEvent& e : engine::ecs::EventReader<engine::PointerTargetEvent>{
             world, world.ctx<engine::ecs::EventCursor<engine::PointerTargetEvent>>()}) {
        switch (e.kind) {
        case engine::PointerTargetEvent::Kind::Enter: highlight(e.entity, true); break;
        case engine::PointerTargetEvent::Kind::Exit: highlight(e.entity, false); break;
        case engine::PointerTargetEvent::Kind::Click: collect(e.entity); break;   // press and release on it
        case engine::PointerTargetEvent::Kind::Press: break;                      // button went down
        }
    }
}
```

`PointerTarget::hovered` and `pressed` hold the same state if you would rather poll. Rules:

- The rectangle is in the entity's space: it follows `Transform` (position, rotation, scale) and a `Parent`. The camera's pan and zoom, and each window's size, are applied for you.
- Overlapping targets: the topmost wins, using the sprite's `layer` and `order_in_layer` (the target's own fields when the entity has no sprite), then the later entity, which is the draw order. `enabled = false` takes a target out. `window` limits it to one window.
- UI first. A button, scroll area, or popup under the cursor takes the pointer: the target below it gets no hover, press, or click, and an `Exit` if it was hovered. Plain labels and empty panels do not take the pointer.
- A window with a target under the cursor is added to `Presentation.mouse`, so a transparent click-through window keeps the click, and a gameplay system that checks `consumed_for(window)` before acting on the ground ignores that click too. The order (UI, then targets, then click-through) is fixed by the engine's system order, so you cannot call it in the wrong order.

---

## 4. Key Repeat Handling (`KeyEvent`)

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
