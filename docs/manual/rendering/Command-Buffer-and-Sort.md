# Command Buffer & Sorting

Rendering in Wind is data-driven. A game does not issue draw calls: it puts `Renderable`, `Sprite`, and `ParticleEmitter` components on entities that have a `Transform`. The engine's render system (`Schedule::Frame`, `Phase::Render`) turns them into commands, sorts them once per frame, and pushes them into the `engine::render::CommandBuffer` of every window the world is bound to. The backend then executes the buffer.

---

## 1. The `Renderable` Component

An entity drawn as a mesh with a material carries an `engine::render::Renderable` and an `engine::Transform`:

```cpp
#include <engine/builtin_ids.h>
#include <engine/ecs/transform.h>
#include <engine/render/renderable.h>
#include <engine/resources/assets_db.h>
#include <asset_ids.h>

engine::ecs::Entity tile = world.create();
world.emplace<engine::Transform>(tile, engine::Transform{.position = {2.0f, 1.0f, 0.0f}, .scale = {1.5f, 1.5f, 1.0f}});
world.emplace<engine::render::Renderable>(tile, engine::render::Renderable{
    .mesh = services.assets.get<engine::render::IMesh>(engine::builtin::mesh_quad),
    .material = services.assets.get<engine::render::IMaterial>(assets::materials::board),
    .color = {1.0f, 1.0f, 1.0f, 1.0f},   // tint, multiplied by the material color
    .layer = 0,
    .order_in_layer = 10,
});
```

`Renderable` has `mesh`, `material` (both required: an entity missing one is reported and skipped), `color`, `layer`, `order_in_layer`, and an optional `material_override`. The mesh is drawn with the model matrix of the `Transform` (translate, rotate X then Y then Z in radians, scale). For a textured picture use a `Sprite` ([Sprites & Animation](Sprites-and-Animation.md)), which supplies the quad and the material for you. An entity with both `Renderable` and `Sprite` draws only the `Renderable`.

---

## 2. Draw Order

Draws of meshes, sprites, and particle batches are sorted together; the first difference decides:

1. `layer` ascending (a low layer is drawn first, behind)
2. `order_in_layer` ascending
3. the material (entities that share a material are grouped; the order between two materials is by address)
4. the entity index

There is no depth test and no Y-sort. For a top-down game, set `order_in_layer` from the entity's Y yourself. UI is separate: canvases are drawn after the world, sorted by `UiCanvas::order`.

---

## 3. What a `CommandBuffer` Is

`engine::render::CommandBuffer` holds a `std::vector` of `render::Command`, a variant of exactly three types:

| Command | Pushed by |
| --- | --- |
| `CmdDrawMesh` | the render system, for each `Renderable` and `Sprite` |
| `CmdDrawParticles` | the render system, for each `ParticleEmitter` with live particles |
| `CmdDrawUI` | the UI system, for each `UiCanvas` |

A game does not push commands itself and there is no custom draw callback. If you need something the components do not give you, the options are a material with your own shader ([Materials & Shaders](Materials-and-Shaders.md)) and, for vector shapes in the UI, `IPaint` ([Custom Painting](../ui/Custom-Painting.md)). The buffer is exposed (`services.commands`, `size()`, iteration) for tests and tools: with a headless `Host` a test can run a frame and inspect which commands were produced.

---

## Next Steps

- Set up camera views in [Camera](Camera.md).
- Create in-game interfaces in [UI Basics](../ui/UI-Basics.md).
