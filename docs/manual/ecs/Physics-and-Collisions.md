# Physics & Collisions

Wind provides an integrated collision probe for boxes and circles: velocity integration, overlap and trigger testing, and `CollisionEvent`s. It is a *probe*, not a physics solver. There is no contact resolution, no collision response, and no Z test: overlaps are tested on the XY plane only, and reacting to a hit is up to your systems.

---

## 1. Colliders

Attach `engine::BoxCollider` (or `engine::CircleCollider`) and `engine::Transform` to entities. Colliders are centered on `Transform::position`. These types live in `engine`, not `engine::ecs`:

```cpp
#include <engine/ecs/physics.h>
#include <engine/ecs/transform.h>

// Spawn a solid wall
auto wall = world.create();
world.emplace<engine::Transform>(wall, engine::Transform{.position = {400.0f, 300.0f, 0.0f}});
world.emplace<engine::BoxCollider>(wall, engine::BoxCollider{
    .size = {64.0f, 64.0f, 1.0f},   // full extents, not half extents
    .layer = 1u << 0,               // what this collider is
    .mask = 1u << 1,                // what it detects (the player's layer)
    .is_trigger = false
});

// Spawn a coin trigger
auto coin = world.create();
world.emplace<engine::Transform>(coin, engine::Transform{.position = {200.0f, 150.0f, 0.0f}});
world.emplace<engine::CircleCollider>(coin, engine::CircleCollider{
    .radius = 16.0f,
    .layer = 1u << 2,
    .mask = 1u << 1,
    .is_trigger = true   // a trigger still reports Enter/Stay/Exit; the flag lets you tell it apart
});
```

Two colliders are tested against each other only when each one's `layer` hits the other's `mask`: `(a.layer & b.mask) != 0 && (b.layer & a.mask) != 0`. Give the player `layer = 1u << 1` and a `mask` that includes the wall and coin layers.

An entity with `engine::RigidBody` (a `velocity`) and a `Transform` is moved by `position += velocity * fixed_delta_time` in the same pass.

---

## 2. Physics Runs by Itself

The engine registers `run_physics` as a system on `Schedule::Fixed`, `Phase::Physics` for every world. You do not call it. Registering it again would integrate velocities twice. Put your own fixed logic on `Phase::Game`, which runs after it:

```cpp
void MyGame::on_start() {
    world().add_system(
        engine::ecs::Schedule::Fixed,
        engine::ecs::Phase::Game,
        [](engine::ecs::World& w) {
            // Runs after the engine's physics pass of the same step: read CollisionEvent here
        }
    );
}
```

`engine::run_physics(world)` is public so a test can run one step by hand.

---

## 3. Handling `CollisionEvent`

For every overlapping pair each fixed step, `run_physics` sends an `engine::CollisionEvent`. A pair that starts overlapping reports `Enter`, keeps reporting `Stay` while it overlaps, and reports `Exit` once when it stops:

```cpp
#include <engine/ecs/events.h>
#include <engine/ecs/physics.h>

void collision_system(engine::ecs::World& world) {
    using engine::ecs::EventCursor;
    using engine::ecs::EventReader;

    for (const engine::CollisionEvent& col : EventReader<engine::CollisionEvent>{
                 world, world.ctx<EventCursor<engine::CollisionEvent>>()}) {
        // col.a, col.b      the two entities (ordered, a < b)
        // col.phase         CollisionPhase::Enter, Stay or Exit
        // col.is_trigger    true if either collider is a trigger
        if (col.phase == engine::CollisionPhase::Enter) {
            handle_hit(world, col.a, col.b);
        }
    }
}
```

There is no contact normal or penetration depth. If you need a response (stopping a player at a wall), compute it from the two `Transform`s and colliders in your own system.

---

## Next Steps

- Render your entities with sprites in [Sprites & Animation](../rendering/Sprites-and-Animation.md).
- Control the viewport using the [Camera](../rendering/Camera.md).
