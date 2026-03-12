# Action Mapping

Wind decouples physical hardware devices (keyboards, mice, gamepads) from gameplay code through the named action system in `engine::InputSystem`.

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
    auto& input = services_.input;

    // 1. Intern action names once
    const engine::ActionId jump = input.intern("jump");
    const engine::ActionId fire = input.intern("fire");
    const engine::ActionId pause = input.intern("pause");

    // 2. Bind keyboard keys
    input.bind(engine::KeyCode::Space, jump);
    input.bind(engine::KeyCode::W, jump);
    input.bind(engine::KeyCode::Escape, pause);

    // 3. Bind mouse buttons
    input.bind(engine::MouseButton::Left, fire);
}
```

---

## 3. Reading Action Events

Gameplay systems consume actions from `world.ctx<Events<InputEvent>>()`:

```cpp
#include <engine/core/input_system.h>
#include <engine/ecs/events.h>

class PlayerInputSystem {
public:
    void update(engine::ecs::World& world, engine::InputSystem& input) {
        auto& events = world.ctx<engine::ecs::Events<engine::InputEvent>>();

        for (const auto& ev : reader_.read(events)) {
            if (ev.kind == engine::InputEvent::Kind::Down) {
                if (ev.action == action_jump_) {
                    trigger_jump();
                } else if (ev.action == action_fire_) {
                    trigger_fire();
                }
            }
        }
    }

private:
    engine::ecs::EventReader<engine::InputEvent> reader_;
    engine::ActionId action_jump_{};
    engine::ActionId action_fire_{};
};
```

---

## 4. Checking Held Actions

To test whether an action is continuously held (for movement or charging):

```cpp
if (services_.input.is_held(action_move_right_)) {
    velocity.x += 10.0f;
}
```

---

## Next Steps

- Handle mouse movement, drag, and key repeat in [Raw Input & Mouse](Raw-Input-and-Mouse.md).
- Support multiple windows in [Multi-Window Input](Multi-Window-Input.md).
