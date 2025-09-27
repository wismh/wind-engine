# Events

Wind provides a double-buffered event queue (`engine::ecs::Events<T>`) designed to let systems communicate without direct coupling. There is no global event bus and no subscribe/callback API: a system sends into a queue and other systems read from it.

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

Send through an `engine::ecs::EventWriter<T>`, which finds (or creates) the world's `Events<T>` queue:

```cpp
#include <engine/ecs/events.h>
#include <engine/ecs/world.h>

void combat_system(engine::ecs::World& world) {
    engine::ecs::EventWriter<game::DamageEvent> damage{world};

    // Deal 25 damage to target
    damage.send(game::DamageEvent{.target = enemy_entity, .amount = 25});
}
```

`world.ctx<engine::ecs::Events<T>>().send(...)` does the same.

---

## 3. Reading Events

A system reads with an `EventReader<T>` built from the world **and a persistent cursor**. The cursor remembers how far this reader has read, so each event is seen exactly once:

```cpp
void health_system(engine::ecs::World& world) {
    using engine::ecs::EventCursor;
    using engine::ecs::EventReader;

    for (const game::DamageEvent& ev : EventReader<game::DamageEvent>{
                 world, world.ctx<EventCursor<game::DamageEvent>>()}) {
        if (auto* health = world.try_get<Health>(ev.target)) {
            health->current -= ev.amount;
            if (health->current <= 0) {
                engine::ecs::EventWriter<game::PlayerDiedEvent>{world}.send(
                        game::PlayerDiedEvent{.player = ev.target});
            }
        }
    }
}
```

`world.ctx<EventCursor<T>>()` is one shared cursor per event type and world, right when exactly one system reads that type every frame. A second independent reader of the same type holds its own `EventCursor<T>`.

`EventReader<T>{world}` (or `{events}`) without a cursor returns **every** event still in the buffer, every time. That fits a one-off read such as a test. A system that builds it each frame sees an event on the frame it was sent and again on the next.

---

## 4. Double-Buffering Lifecycle

`Events<T>` maintains two internal generations:
1. **Current:** Collects all events sent since the last flush.
2. **Previous:** Contains the events of the generation before.

The engine calls `world.flush_events()` once per frame, at the start of the tick before any system runs: the previous generation is dropped and the current one becomes the previous one. So an event stays readable for the rest of the frame it was sent in and for the whole next frame. A fixed step (`Schedule::Fixed`) can run several times in a frame, and a cursor reader sees each event once across all of them.

> [!NOTE]
> An event sent in a frame is visible to readers that run later in the same frame and to every reader in the next one, so the order in which systems run does not make an event vanish. A reader that falls more than one generation behind misses the events that were dropped.

The engine itself uses the same queues: `CollisionEvent` ([Physics & Collisions](Physics-and-Collisions.md)), `InputEvent`, `MouseEvent`, `KeyEvent` ([Input](../input/Action-Mapping.md)), `PlaySfxEvent` ([Sound & Music](../audio-and-haptics/Sound-and-Music.md)), and `WindowCloseRequestedEvent`.

---

## Next Steps

- Integrate collision events with [Physics & Collisions](Physics-and-Collisions.md).
- Connect gameplay events to sound in [Sound & Music](../audio-and-haptics/Sound-and-Music.md).
