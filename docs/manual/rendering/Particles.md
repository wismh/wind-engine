# Particle Systems

Wind includes a high-performance 2D/3D particle system via `engine::render::ParticleEmitter` (`include/engine/render/particles.h`). Particles support customizable emission shapes, size and color animations via curves, multi-threaded CPU simulation through `JobSystem`, 2D box and circle physics colliders, and batch rendering.

---

## 1. The `ParticleEmitter` Component

To create a particle effect, attach `engine::render::ParticleEmitter` and `engine::Transform` to an entity:

```cpp
#include <engine/ecs/transform.h>
#include <engine/render/particles.h>
#include <engine/resources/assets_db.h>
#include <asset_ids.h>

void spawn_explosion(engine::ecs::World& world, engine::AssetsDb& assets, glm::vec3 position) {
    engine::ecs::Entity emitter_entity = world.create();
    world.emplace<engine::Transform>(emitter_entity, engine::Transform{.position = position});

    engine::render::ParticleEmitter emitter;
    emitter.texture = assets.get<engine::render::ITexture>(assets::textures::smoke_particle);
    emitter.blend = engine::render::BlendMode::Additive; // or BlendMode::Alpha
    emitter.layer = 1;
    emitter.order_in_layer = 100;

    // Emission rate and capacity
    emitter.emission_rate = 50.0f; // particles per second
    emitter.max_particles = 200;
    emitter.looping = true;
    emitter.playing = true;

    // Lifetime & Speed
    emitter.lifetime_min = 0.5f;
    emitter.lifetime_max = 1.2f;
    emitter.speed_min = 2.0f;
    emitter.speed_max = 5.0f;
    emitter.direction = {0.0f, 1.0f, 0.0f};
    emitter.spread_angle = glm::radians(45.0f);

    // Size over lifetime (start -> end)
    emitter.size_start_min = {0.2f, 0.2f};
    emitter.size_start_max = {0.4f, 0.4f};
    emitter.size_end_min = {0.8f, 0.8f};
    emitter.size_end_max = {1.2f, 1.2f};

    // Color gradient over lifetime (linear)
    emitter.color_start = {1.0f, 0.8f, 0.2f, 1.0f}; // Orange-yellow
    emitter.color_end = {0.8f, 0.1f, 0.0f, 0.0f};   // Fade to dark red / transparent

    // Physics
    emitter.gravity = {0.0f, -4.0f, 0.0f};
    emitter.simulation_space = engine::render::SimulationSpace::World; // or SimulationSpace::Local

    world.emplace<engine::render::ParticleEmitter>(emitter_entity, std::move(emitter));
}
```

---

## 2. Emission Shapes

Wind provides several emitter shapes (`EmitterShape`):

| Shape | Description | Relevant Fields |
| --- | --- | --- |
| `EmitterShape::Point` | All particles spawn at the emitter's origin. | (Default) |
| `EmitterShape::Box` | Spawns uniformly inside an axis-aligned box volume. | `emitter.shape_scale = {w, h, d}` |
| `EmitterShape::Circle` | Spawns particles uniformly within an XY circle or disc. | `emitter.shape_scale.x` (radius) |
| `EmitterShape::Cone` | Spawns at apex and directs within `spread_angle`. | `emitter.direction`, `emitter.spread_angle` |

---

## 3. Animation Curves (`Curve<T>`)

Instead of simple linear interpolation between start and end values, you can provide fine-grained keyframed animation curves evaluated over normalized lifetime $t \in [0, 1]$:

```cpp
// Non-linear scale pulse over lifetime
emitter.size_curve = engine::render::Curve<float>{
    .keys = {
        {0.0f, 0.1f}, // spawn small
        {0.2f, 1.5f}, // pop up quickly
        {1.0f, 0.0f}  // shrink to zero
    }
};

// Alpha fade curve
emitter.alpha_curve = engine::render::Curve<float>{
    .keys = {
        {0.0f, 0.0f},
        {0.1f, 1.0f},
        {0.7f, 1.0f},
        {1.0f, 0.0f}
    }
};
```

When custom curves are non-empty, they override linear start/end values.

---

## 4. Simulation Space

- **`SimulationSpace::World`**: Once spawned, particles move independently of the emitter entity. Moving or rotating the emitter leaves previously spawned particles behind in world coordinates (ideal for smoke trails, sparks, explosions).
- **`SimulationSpace::Local`**: Particles transform with the emitter's parent entity (ideal for jet thrusters, glowing auras, magic shields).

---

## 5. Particle 2D Collisions

Particles can collide with 2D obstacles:

```cpp
emitter.collision_enabled = true;
emitter.collision_mask = (1u << 0); // Collide with layer 0 (e.g. solid ground)
emitter.bounce = 0.6f;             // Coefficient of restitution [0, 1]
emitter.friction = 0.2f;           // Tangential friction damping
emitter.kill_on_collision = false; // Set true to destroy immediately on hit
emitter.collision_radius = 0.05f;  // Particle thickness (0.0f treats as point)
```

During simulation, the engine copies non-trigger `BoxCollider2D` and `CircleCollider2D` components and reflects particle velocities with the emitter's `bounce` and `friction`. Those fields are not the collider's `restitution` and `friction`. Polygons are not copied, and a box is copied axis-aligned, so box yaw is ignored. A collider is skipped when `(layer & collision_mask) == 0`.

---

## 6. Runtime Playback & Bursts

```cpp
auto& emitter = world.get<engine::render::ParticleEmitter>(entity);

// Immediate instantaneous emission of N particles
emitter.burst(30);

// Playback control
emitter.pause();
emitter.play();
emitter.stop(); // Clears all active particles and resets elapsed timers

// Query active particle count
std::size_t active = emitter.active_count();
```

---

## 7. Engine Execution & Multithreading

- The engine registers particle simulation on `Schedule::Frame`, `Phase::Game`.
- If a `JobSystem` is present on `EngineServices`, particle integration runs in parallel across worker threads (`parallel_for`), while spawning and particle compaction occur safely on the main thread.
- Rendering occurs on `Phase::Render`, sorted with other 2D/3D elements in `CommandBuffer` via `CmdDrawParticles`.

---

## Next Steps

- Draw background grids with [Tilemaps](Tilemaps.md).
- Create custom shaders for particles with [Materials & Shaders](Materials-and-Shaders.md).
- Control rendering passes in [Command Buffer & Sorting](Command-Buffer-and-Sort.md).
