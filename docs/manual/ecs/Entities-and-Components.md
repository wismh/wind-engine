# Entities & Components

Wind includes a lightweight, cache-friendly, generational Entity Component System (ECS) located in the `engine::ecs` namespace.

---

## 1. Generational Entities

An `engine::ecs::Entity` is an opaque 64-bit integer combining a 32-bit slot index and a 32-bit generation counter:

```cpp
#include <engine/ecs/world.h>
#include <engine/ecs/entity.h>

engine::ecs::World world;

// Create an entity
engine::ecs::Entity e = world.create();

// Check if an entity is alive
bool alive = world.valid(e); // true

// Destroy an entity
world.destroy(e);

// An entity handle is invalidated after destruction
bool still_alive = world.valid(e); // false
```

The generation counter ensures that stale entity handles held by systems, scripts, or events never accidentally point to newly spawned entities occupying the recycled slot.

---

## 2. Defining Components

Components in Wind are plain old C++ structs. No base class or registration macros are needed:

```cpp
#include <glm/vec2.hpp>

namespace game {

struct Position {
    glm::vec2 value{0.0f, 0.0f};
};

struct Velocity {
    glm::vec2 value{0.0f, 0.0f};
};

struct PlayerTag {};

} // namespace game
```

---

## 3. Managing Components on Entities

Use `world.emplace`, `world.get`, `world.try_get`, and `world.remove`:

```cpp
engine::ecs::Entity player = world.create();

// Attach components
world.emplace<game::Position>(player, glm::vec2{100.0f, 200.0f});
world.emplace<game::Velocity>(player, glm::vec2{10.0f, 0.0f});
world.emplace<game::PlayerTag>(player);

// Access component reference (asserts if missing)
game::Position& pos = world.get<game::Position>(player);

// Safe pointer access (returns nullptr if missing)
if (game::Velocity* vel = world.try_get<game::Velocity>(player)) {
    pos.value += vel->value;
}

// Remove component
world.remove<game::Velocity>(player);
```

---

## 4. Querying Entities with Views

Use `world.view<Ts...>()` to iterate entities possessing all specified components:

```cpp
// Single-component view
for (auto [entity, pos] : world.view<game::Position>().each()) {
    pos.value.y += 1.0f;
}

// Multi-component view
for (auto [entity, pos, vel] : world.view<game::Position, game::Velocity>().each()) {
    pos.value += vel.value * dt;
}
```

### Destruction Safety During Iteration
Destroying entities while iterating views is completely safe:
```cpp
for (auto [entity, pos] : world.view<game::Position>().each()) {
    if (pos.value.y > 1000.0f) {
        world.destroy(entity); // Safe! Defers deletion until the view finishes
    }
}
```
`World` defers structural entity destructions until all active views exit.

---

## 5. Singleton Resources (`world.ctx<T>()`)

For shared game state that isn't tied to a specific entity (like game settings, global score, or level maps), use `world.ctx<T>()`:

```cpp
struct GameScore {
    int points = 0;
};

// Access or auto-default-construct the resource
world.ctx<GameScore>().points += 100;

// Read the resource
int current = world.ctx<GameScore>().points;
```

---

## Next Steps

- Learn how to structure systems across fixed and frame loops in [Systems & Schedules](Systems-and-Schedules.md).
- Learn how systems communicate asynchronously in [Events](Events.md).
