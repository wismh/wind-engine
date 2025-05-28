# Physics & Collisions

Wind provides an integrated 2D collision probe system for Axis-Aligned Bounding Box (AABB) intersection and trigger testing.

---

## 1. Colliders

Attach `engine::ecs::BoxCollider` and `engine::ecs::Transform` to entities:

```cpp
#include <engine/ecs/physics.h>
#include <engine/ecs/transform.h>

// Spawn a solid wall
auto wall = world.create();
world.emplace<engine::ecs::Transform>(wall, glm::vec2{400.0f, 300.0f});
world.emplace<engine::ecs::BoxCollider>(wall, engine::ecs::BoxCollider{
    .half_extents = {32.0f, 32.0f},
    .layer = 1 << 0,  // Layer mask
    .mask = 1 << 1,   // Collides with player (layer 1)
    .is_trigger = false
});

// Spawn a coin trigger
auto coin = world.create();
world.emplace<engine::ecs::Transform>(coin, glm::vec2{200.0f, 150.0f});
world.emplace<engine::ecs::BoxCollider>(coin, engine::ecs::BoxCollider{
    .half_extents = {16.0f, 16.0f},
    .layer = 1 << 2,
    .mask = 1 << 1,
    .is_trigger = true // Triggers fire collision events without rigid collision response
});
```

---

## 2. Running Physics Detection

Execute `engine::ecs::run_physics(world)` in your `Schedule::Fixed` physics phase:

```cpp
void MyGame::on_start() {
    world().add_system(
        engine::ecs::Schedule::Fixed,
        engine::ecs::Phase::Physics,
        [](engine::ecs::World& w) {
            // Evaluates all colliders and emits CollisionEvent entries
            engine::ecs::run_physics(w);
        }
    );
}
```

---

## 3. Handling `CollisionEvent`

When two colliders overlap, `run_physics` emits an `engine::ecs::CollisionEvent`:

```cpp
#include <engine/ecs/events.h>
#include <engine/ecs/physics.h>

class CollisionSystem {
public:
    void update(engine::ecs::World& world) {
        auto& events = world.ctx<engine::ecs::Events<engine::ecs::CollisionEvent>>();

        for (const auto& col : reader_.read(events)) {
            // col.entity_a, col.entity_b
            // col.normal (collision manifold normal)
            // col.penetration (overlap depth)

            handle_hit(world, col.entity_a, col.entity_b);
        }
    }

private:
    engine::ecs::EventReader<engine::ecs::CollisionEvent> reader_;
};
```

---

## Next Steps

- Render your entities with sprites in [Sprites & Animation](../rendering/Sprites-and-Animation.md).
- Control the viewport using the [Camera](../rendering/Camera.md).
