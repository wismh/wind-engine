# Entities & Components

Wind includes a lightweight, cache-friendly, generational Entity Component System (ECS) located in the `engine::ecs` namespace.

---

## 1. Generational Entities

An `engine::ecs::Entity` is a small value type: a 32-bit slot `index` and a 32-bit `generation` counter. It compares with `==` and `<`.

```cpp
#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>

engine::ecs::World world;   // in a game you use world() from GameBase

// Create an entity
engine::ecs::Entity e = world.create();

// Check if an entity is alive
bool alive = world.valid(e); // true

// Destroy an entity
world.destroy(e);

// An entity handle is invalidated after destruction
bool still_alive = world.valid(e); // false
```

The generation counter ensures that stale entity handles held by systems or events never accidentally point to newly spawned entities occupying the recycled slot. `try_get` on a destroyed entity returns `nullptr`.

---

## 2. Defining Components

Components in Wind are plain old C++ structs. No base class or registration macros are needed. The engine's own components (`engine::Transform`, `engine::Camera`, `engine::RigidBody`, `engine::BoxCollider`, `engine::render::Sprite`, ...) live in `engine` and `engine::render`, not in `engine::ecs`.

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

// Attach components (the arguments construct the component in place)
world.emplace<game::Position>(player, glm::vec2{100.0f, 200.0f});
world.emplace<game::Velocity>(player, glm::vec2{10.0f, 0.0f});
world.emplace<game::PlayerTag>(player);

// Access component reference (asserts if missing)
game::Position& pos = world.get<game::Position>(player);

// Safe pointer access (returns nullptr if missing)
if (game::Velocity* vel = world.try_get<game::Velocity>(player)) {
    pos.value += vel->value;
}

// Remove component (a no-op when the entity does not have it)
world.remove<game::Velocity>(player);
```

`emplace` asserts that the entity is alive and does not already have that component.

---

## 4. Querying Entities with Views

`world.view<Ts...>()` iterates the entities that have **all** the listed components (at least one type is required). Iterating a view yields `Entity` values; fetch the components with `view.get<T>(entity)`:

```cpp
auto view = world.view<game::Position, game::Velocity>();
for (engine::ecs::Entity entity : view) {
    auto& pos = view.get<game::Position>(entity);
    const auto& vel = view.get<game::Velocity>(entity);
    pos.value += vel.value * dt;
}
```

`view.each(fn)` calls a function for each match, either with the components (`[&](game::Position& pos, game::Velocity& vel)`) or with the entity first (`[&](engine::ecs::Entity e, game::Position& pos, game::Velocity& vel)`):

```cpp
world.view<game::Position, game::Velocity>().each(
        [dt](game::Position& pos, const game::Velocity& vel) { pos.value += vel.value * dt; });
```

### Destruction Safety During Iteration
Destroying entities while iterating views is safe:
```cpp
auto view = world.view<game::Position>();
for (engine::ecs::Entity entity : view) {
    if (view.get<game::Position>(entity).value.y > 1000.0f) {
        world.destroy(entity); // Safe! Defers deletion until the view finishes
    }
}
```
`World` defers structural entity destructions until all active views exit.

---

## 5. Singleton Resources (`world.ctx<T>()`)

For shared game state that isn't tied to a specific entity (like game settings, global score, or level maps), use `world.ctx<T>()`. There is one value of each type per world:

```cpp
struct GameScore {
    int points = 0;
};

// Access or auto-default-construct the resource
world.ctx<GameScore>().points += 100;

// Read the resource
int current = world.ctx<GameScore>().points;
```

The engine keeps its own state the same way: `world.ctx<engine::Time>()`, `world.ctx<engine::ActiveCamera>()`, `world.ctx<engine::loc::Catalog>()`.

---

## Next Steps

- Learn how to structure systems across fixed and frame loops in [Systems & Schedules](Systems-and-Schedules.md).
- Learn how systems communicate asynchronously in [Events](Events.md).
