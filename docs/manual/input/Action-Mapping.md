# Action Mapping

Wind decouples physical controls (keyboard keys, mouse buttons, touch) from gameplay code through the named action system in `engine::InputSystem`. Gamepads are not wired up yet: only keys, mouse buttons, and touch reach actions.

---

## 1. Principle: Named Actions (`ActionId`)

> [!IMPORTANT]
> **Rule:** Gameplay systems must compare `engine::ActionId`, never physical `engine::KeyCode`.

Instead of checking `if (key == KeyCode::Space)`, intern an action name (e.g. `"jump"`), bind keys to that action, and listen for `engine::InputEvent`.

---

## 2. Setting Up Bindings in `on_start()`

```cpp
#include <engine/core/input_system.h>

void MyGame::on_start() {
    engine::InputSystem& input = services_.input;

    // 1. Intern action names once and keep the ids
    jump_ = input.intern("jump");
    fire_ = input.intern("fire");
    pause_ = input.intern("pause");

    // 2. Bind keyboard keys (several controls may share one action)
    input.bind(engine::KeyCode::Space, jump_);
    input.bind(engine::KeyCode::W, jump_);
    input.bind(engine::KeyCode::Escape, pause_);

    // 3. Bind mouse buttons
    input.bind(engine::MouseButton::Left, fire_);
}
```

`jump_`, `fire_`, and `pause_` are `engine::ActionId` members of the game. `bind` replaces whatever action that control had; `unbind` removes it. There is also `bind(KeyCode, "name")`, which interns the name for you. A game that rebinds keys in a settings screen calls `bind` again with the new key.

---

## 3. Reading Action Events

Every press or release of a bound control becomes an `engine::InputEvent` (`action`, `kind` of `Down` or `Up`, `value`) in the world's event queue. Read it with a cursor, like any event ([Events](../ecs/Events.md)):

```cpp
#include <engine/core/input_system.h>
#include <engine/ecs/events.h>

void MyGame::read_input(engine::ecs::World& world) {   // registered on Schedule::Frame, Phase::Game
    using engine::ecs::EventCursor;
    using engine::ecs::EventReader;

    for (const engine::InputEvent& ev :
            EventReader<engine::InputEvent>{world, world.ctx<EventCursor<engine::InputEvent>>()}) {
        if (ev.kind != engine::InputEvent::Kind::Down) {
            continue;
        }
        if (ev.action == jump_) {
            trigger_jump();
        } else if (ev.action == fire_) {
            trigger_fire();
        }
    }
}
```

Register it in `on_start`: `world().add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game, [this](engine::ecs::World& w) { read_input(w); });`.

An action bound to a mouse button fires even when the click landed on a UI button. `InputSystem` does not filter by UI: check whether the UI consumed the mouse ([Preventing Click-Through](Raw-Input-and-Mouse.md#2-preventing-click-through)) in the handler.

---

## 4. Checking Held Actions

To test whether an action is continuously held (for movement or charging):

```cpp
if (services_.input.is_held(move_right_)) {
    velocity.x += 10.0f;
}
```

`is_held` is true while at least one control bound to that action is down. For smooth movement, scale by `Time::fixed_delta_time` in a fixed-step system.

---

## 5. Android Back

The engine never quits on the Back key. Bind `KeyCode::AcBack` to an action of your own and handle it ([Android](../platforms/Android.md#the-back-button)).

---

## Next Steps

- Handle mouse movement, drag, and key repeat in [Raw Input & Mouse](Raw-Input-and-Mouse.md).
- Support multiple windows in [Multi-Window Input](Multi-Window-Input.md).
