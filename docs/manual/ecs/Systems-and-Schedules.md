# Systems & Schedules

Systems contain your game logic and operate on components stored in `ecs::World`. They are organized into **Schedules** and executed within ordered **Phases**.

---

## 1. Schedules

Wind defines two schedules in `engine::ecs::Schedule`:

1. **`Schedule::Fixed`**: Runs 60 times per second with fixed delta time. Suitable for physics, movement, and collision updates.
2. **`Schedule::Frame`**: Runs once per rendered frame with variable delta time. Suitable for input processing, UI data binding, audio triggers, and drawing submission.

---

## 2. Execution Phases

Within each schedule, systems execute sequentially across predefined phases (`engine::ecs::Phase`):

### `Schedule::Fixed` Phases
```
1. Phase::Physics  --> Physics solvers, velocity integration, collision detection
2. Phase::Game     --> Fixed gameplay logic, enemy AI, simulation steps
```

### `Schedule::Frame` Phases
```
1. Phase::Input    --> Window and hardware input collection
2. Phase::Game     --> Visual interpolation, gameplay animations, camera updates
3. Phase::Bind     --> UI ViewModel data synchronization
4. Phase::Audio    --> Audio event processing and music playback
5. Phase::Render   --> Game rendering, sprite sorting, CommandBuffer submission
6. Phase::UiRender --> UI layout, document painting, NanoVG rendering
```

---

## 3. Registering Systems

Register systems in your `MyGame::on_start()` method:

```cpp
#include <engine/ecs/schedule.h>
#include <engine/ecs/world.h>

void player_movement_system(engine::ecs::World& world) {
    const auto& time = world.ctx<engine::Time>();
    for (auto [e, pos, vel] : world.view<Position, Velocity>().each()) {
        pos.value += vel.value * time.fixed_delta_time;
    }
}

void camera_follow_system(engine::ecs::World& world) {
    // Smooth camera tracking using variable frame delta time
}

void MyGame::on_start() {
    // Register fixed gameplay physics
    world().add_system(
        engine::ecs::Schedule::Fixed,
        engine::ecs::Phase::Physics,
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

---

## 4. Writing Clean Systems

### Keep Systems Stateless
Systems are free functions or static methods. Avoid capturing mutable global state inside system lambdas; instead, store shared state in `world.ctx<T>()` resources or inside components.

```cpp
// GOOD
void update_score_system(engine::ecs::World& world) {
    auto& score = world.ctx<GameScore>();
    // ...
}

// BAD - Hidden state inside lambda
int hidden_counter = 0;
world.add_system(Schedule::Frame, Phase::Game, [hidden_counter](World& w) mutable {
    hidden_counter++;
});
```

---

## Next Steps

- Decouple systems using the [Events](Events.md) queue.
- Implement 2D collisions with [Physics & Collisions](Physics-and-Collisions.md).
