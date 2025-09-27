# Systems & Schedules

Systems contain your game logic and operate on components stored in `ecs::World`. They are organized into **Schedules** and executed within ordered **Phases**. A system is a `std::function<void(engine::ecs::World&)>`: a free function, or a lambda.

---

## 1. Schedules

Wind defines two schedules in `engine::ecs::Schedule`:

1. **`Schedule::Fixed`**: Runs in steps of `engine::kFixed` (1/60 s), zero to eight times per frame depending on how much real time has passed. Suitable for movement, physics, and collision updates. It reads `Time::fixed_delta_time`.
2. **`Schedule::Frame`**: Runs once per rendered frame with variable delta time (`Time::delta_time`). Suitable for input handling, camera smoothing, UI data binding, audio triggers, and one-shot clicks.

---

## 2. Execution Phases

Within each schedule, systems execute sequentially across predefined phases (`engine::ecs::Phase`). Systems in the same phase run in the order they were registered:

### `Schedule::Fixed` Phases
```
1. Phase::Physics  --> Engine: run_physics (velocity integration, collision events)
2. Phase::Game     --> Fixed gameplay logic, enemy AI, simulation steps
```

### `Schedule::Frame` Phases
```
1. Phase::Input    --> Engine: window and hardware input delivered to the UI
2. Phase::Game     --> Engine: sprite animation, particles. Yours: gameplay, camera updates
3. Phase::Bind     --> Engine: UI bindings (ViewModel -> document)
4. Phase::Audio    --> Engine: PlaySfxEvent / PlayMusicEvent
5. Phase::Render   --> Engine: sprites and meshes into the CommandBuffer
6. Phase::UiRender --> Engine: UI canvases into the CommandBuffer
```

`Phase::Physics` is not a frame phase: a system registered on `Frame` with that phase never runs. The engine registers its own systems before your `on_start`, so your `Phase::Game` systems run after the engine's of the same phase. Code that reads what the UI just did this frame (a click) belongs on `Schedule::Frame`, `Phase::Game`.

---

## 3. Registering Systems

Register systems in your `MyGame::on_start()` method:

```cpp
#include <engine/core/time.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/world.h>

void player_movement_system(engine::ecs::World& world) {
    const auto& time = world.ctx<engine::Time>();
    world.view<Position, Velocity>().each([&](Position& pos, const Velocity& vel) {
        pos.value += vel.value * time.fixed_delta_time;
    });
}

void camera_follow_system(engine::ecs::World& world) {
    // Smooth camera tracking using variable frame delta time
}

void MyGame::on_start() {
    // Register fixed gameplay logic
    world().add_system(
        engine::ecs::Schedule::Fixed,
        engine::ecs::Phase::Game,
        player_movement_system
    );

    // Register variable frame camera
    world().add_system(
        engine::ecs::Schedule::Frame,
        engine::ecs::Phase::Game,
        camera_follow_system
    );
}
```

A world's schedules are skipped while the process is paused (the app is in the background) and while the world is not stepping (`Worlds::set_stepping`). See [World & Time](../architecture/World-and-Time.md#4-pausing-the-game).

---

## 4. Writing Clean Systems

### Keep State Out of Hidden Places
Put shared game state in `world.ctx<T>()` resources or in components, not in globals or function-local statics. A lambda may capture the object that owns it (for example the game or a panel, `[this]`), because that object outlives the world's systems; avoid capturing a mutable value *by copy* and counting on it:

```cpp
// GOOD
void update_score_system(engine::ecs::World& world) {
    auto& score = world.ctx<GameScore>();
    // ...
}

// BAD - a counter that lives only inside the lambda: invisible to everything else
// and not saved or reset with the game state
world().add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game,
        [hidden_counter = 0](engine::ecs::World&) mutable { ++hidden_counter; });
```

---

## Next Steps

- Decouple systems using the [Events](Events.md) queue.
- Implement 2D collisions with [Physics & Collisions](Physics-and-Collisions.md).
