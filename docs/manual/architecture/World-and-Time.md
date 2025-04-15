# World & Time

Wind features a dual-rate update loop that separates variable frame rendering from fixed physics and simulation updates.

---

## 1. Frame Step vs Fixed Step

The engine executes two distinct execution loops:

1. **Fixed Step (`Schedule::Fixed`):**
   - Runs at a deterministic 60 Hz (`kFixed = 1.0f / 60.0f`).
   - Uses an internal time accumulator with a safety clamp (`kMaxFixedSteps = 8` per frame) to prevent the "spiral of death" during low frame rates.
   - Ideal for physics, gameplay collisions, AI pathfinding, and deterministic state progression.

2. **Frame Step (`Schedule::Frame`):**
   - Runs once per rendered frame with variable delta time (`Time::delta_time`).
   - Delta time is capped at `kMaxFrameDt = 0.25f` to prevent massive simulation jumps after window hitches.
   - Ideal for camera smoothing, sprite animations, UI data synchronization, and rendering submission.

---

## 2. Reading `engine::Time`

The active `Time` struct is stored as a context resource inside the `World`:

```cpp
#include <engine/core/time.h>

void update_camera_system(engine::ecs::World& world) {
    const auto& time = world.ctx<engine::Time>();

    float dt = time.delta_time;            // Current frame delta time in seconds
    float fixed_dt = time.fixed_delta_time;// 1/60th second
    float alpha = time.alpha;              // Interpolation alpha between fixed ticks [0.0 .. 1.0]
}
```

### Interpolation Alpha (`alpha`)
When rendering entities updated during fixed steps, `time.alpha` provides the interpolation factor between the previous and current fixed states, enabling jitter-free rendering on high refresh rate monitors (120Hz, 144Hz, 240Hz).

---

## 3. Registering Systems to Schedules

Systems are added using `world.add_system(schedule, phase, fn)`:

```cpp
void MyGame::on_start() {
    // 1. Fixed simulation system (Runs at 60 Hz)
    world().add_system(
        engine::ecs::Schedule::Fixed,
        engine::ecs::Phase::Game,
        [](engine::ecs::World& w) {
            // Physics simulation, movement logic
        }
    );

    // 2. Variable frame system (Runs every render frame)
    world().add_system(
        engine::ecs::Schedule::Frame,
        engine::ecs::Phase::Game,
        [](engine::ecs::World& w) {
            // Visual effects, camera tracking
        }
    );
}
```

---

## 4. Pausing the Game

To pause simulation without stopping the UI, animations, or menus:

1. Implement a pause flag on your game state / clock resource.
2. Check the pause flag in your `Schedule::Fixed` or `Schedule::Frame` gameplay systems:

```cpp
struct GameClock {
    bool paused = false;
    float time_scale = 1.0f;
};

void movement_system(engine::ecs::World& world) {
    const auto& clock = world.ctx<GameClock>();
    if (clock.paused) {
        return;
    }

    const auto& time = world.ctx<engine::Time>();
    float dt = time.delta_time * clock.time_scale;
    // Update player movement...
}
```

Because `engine::ecs::Schedule::Frame` continues running, UI interactions and animations remain fully responsive while the game simulation is frozen.

---

## Next Steps

- Explore generational entity management in [Entities & Components](../ecs/Entities-and-Components.md).
- Learn about the order of execution phases in [Systems & Schedules](../ecs/Systems-and-Schedules.md).
