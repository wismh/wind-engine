# Events

Wind provides a double-buffered, frame-deterministic event queue (`engine::ecs::Events<T>`) designed to let systems communicate without direct coupling.

---

## 1. Defining Events

Events are simple value types:

```cpp
#include <engine/ecs/entity.h>
#include <glm/vec2.hpp>

namespace game {

struct DamageEvent {
    engine::ecs::Entity target;
    int amount = 0;
};

struct PlayerDiedEvent {
    engine::ecs::Entity player;
    glm::vec2 death_location{};
};

} // namespace game
```

---

## 2. Emitting Events

To send an event, access `Events<T>` via `world.ctx<Events<T>>()` and call `.send()`:

```cpp
#include <engine/ecs/events.h>

void combat_system(engine::ecs::World& world) {
    auto& damage_events = world.ctx<engine::ecs::Events<game::DamageEvent>>();

    // Deal 25 damage to target
    damage_events.send({
        .target = enemy_entity,
        .amount = 25
    });
}
```

---

## 3. Reading Events

Systems read events by declaring an `EventReader<T>` cursor:

```cpp
class HealthSystem {
public:
    void update(engine::ecs::World& world) {
        auto& events = world.ctx<engine::ecs::Events<game::DamageEvent>>();

        for (const auto& ev : reader_.read(events)) {
            if (auto* health = world.try_get<Health>(ev.target)) {
                health->current -= ev.amount;
                if (health->current <= 0) {
                    world.ctx<engine::ecs::Events<game::PlayerDiedEvent>>().send({
                        .player = ev.target
                    });
                }
            }
        }
    }

private:
    engine::ecs::EventReader<game::DamageEvent> reader_;
};
```

---

## 4. Double-Buffering Lifecycle

`Events<T>` maintains two internal buffers:
1. **Write Buffer:** Collects all events sent during the current frame/step.
2. **Read Buffer:** Contains all events sent during the previous frame/step.

At the boundary between ticks, `world.flush_events()` swaps the buffers and clears the previous read buffer.

> [!NOTE]
> Because buffers are swapped, an event is visible to readers across one full tick cycle, preventing race conditions where the order of system execution determines whether an event is observed.

---

## Next Steps

- Integrate collision events with [Physics & Collisions](Physics-and-Collisions.md).
- Connect gameplay events to sound in [Sound & Music](../audio-and-haptics/Sound-and-Music.md).
