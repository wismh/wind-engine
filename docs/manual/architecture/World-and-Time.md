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

### Frame rate

By default a frame waits for the display (vsync), so the frame step runs at the monitor's refresh rate and an idle game does not burn a CPU core. With every window minimized the engine sleeps to that rate instead. A settings menu can change this for the whole process through `EngineServices::windows`:

```cpp
services_.windows.set_vsync(false);   // frames no longer wait for the display
services_.windows.set_max_fps(120);   // cap while vsync is off; 0 (the default) is no cap
```

The cap is ignored while vsync paces the frame. Web always runs on the browser's animation frame and ignores both. Fixed-step simulation does not change with the frame rate.

---

## 2. Reading `engine::Time`

The active `Time` struct is stored as a context resource inside the `World`:

```cpp
#include <engine/core/time.h>

void update_camera_system(engine::ecs::World& world) {
    const auto& time = world.ctx<engine::Time>();

    float dt = time.delta_time;            // Current frame delta time in seconds (capped at 0.25)
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

The engine pauses by itself when the app goes to the background (Android): `ApplicationState::paused` is set, the `Schedule::Fixed` steps stop, and `Schedule::Frame` keeps running only for worlds that have a window bound, so the UI stays alive. `Worlds::set_stepping(world, false)` stops both schedules of one world. You reach `ApplicationState` through `worlds().application_state()` in a `GameBase`; `quit()` on it ends the game.

To pause gameplay from a menu while the UI, animations, and menus keep running:

1. Implement a pause flag on your game state / clock resource.
2. Check the pause flag in your gameplay systems:

```cpp
struct GameClock {
    bool paused = false;
    float time_scale = 1.0f;
};

void movement_system(engine::ecs::World& world) {   // registered on Schedule::Fixed, Phase::Game
    const auto& clock = world.ctx<GameClock>();
    if (clock.paused) {
        return;
    }

    const auto& time = world.ctx<engine::Time>();
    float dt = time.fixed_delta_time * clock.time_scale;
    // Update player movement...
}
```

A pause flag in your own resource leaves `engine::ecs::Schedule::Frame` running, so UI interactions and animations remain fully responsive while the game simulation is frozen. Engine systems such as `run_physics` (which integrates `RigidBody::velocity`) and sprite animation do not read `GameClock`; zero those velocities yourself while paused. `Worlds::set_stepping(world, false)` freezes a whole world, including its UI.

---

## Next Steps

- Explore generational entity management in [Entities & Components](../ecs/Entities-and-Components.md).
- Learn about the order of execution phases in [Systems & Schedules](../ecs/Systems-and-Schedules.md).
