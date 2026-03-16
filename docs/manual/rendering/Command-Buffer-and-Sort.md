# Command Buffer & Sorting

Rendering in Wind is completely decoupled from system execution order. Systems submit drawing commands into an `engine::render::CommandBuffer`, which are subsequently sorted and executed during the render phase.

---

## 1. The `Renderable` Component

Entities with visual representations carry an `engine::render::Renderable` component defining their sorting keys:

```cpp
#include <engine/render/renderable.h>

struct Renderable {
    int layer = 0;              // Major layer (e.g., Background = -10, World = 0, Foreground = 10)
    int order_in_layer = 0;     // Sub-order within the layer
    float y_sort = 0.0f;        // Optional Y-coordinate for top-down depth sorting
    bool transparent = true;    // Sorting flag
};
```

During rendering, entities are sorted deterministically:
1. Primary key: `layer` (ascending)
2. Secondary key: `order_in_layer` (ascending)
3. Tertiary key: `y_sort` (ascending or descending depending on camera projection)

---

## 2. Submitting Commands to `CommandBuffer`

In your game's render phase system (`Schedule::Frame`, `Phase::Render`), access `services.commands`:

```cpp
#include <engine/render/command_buffer.h>
#include <engine/render/commands.h>

void render_system(engine::ecs::World& world, engine::render::CommandBuffer& cmd_buffer) {
    // Clear the screen
    cmd_buffer.push(engine::render::CmdClear{
        .color = {0.1f, 0.1f, 0.15f, 1.0f},
        .depth = 1.0f,
        .clear_color = true,
        .clear_depth = true
    });

    // Draw textured meshes
    for (auto [e, transform, sprite, renderable] : 
         world.view<engine::ecs::Transform, engine::render::Sprite, engine::render::Renderable>().each()) {
         
        cmd_buffer.push(engine::render::CmdDrawMesh{
            .material = sprite.material_id,
            .mesh = quad_mesh_id,
            .transform = transform.matrix(),
            .layer = renderable.layer,
            .order = renderable.order_in_layer
        });
    }
}
```

---

## 3. Custom Vector Drawing via `ICanvas`

For immediate-mode vector drawing (debug shapes, bounding boxes, trajectories, lines), use `services.canvas`:

```cpp
#include <engine/render/canvas.h>

services.canvas.draw_line({100.0f, 100.0f}, {400.0f, 300.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, 2.0f);
services.canvas.draw_rect({50.0f, 50.0f, 200.0f, 100.0f}, {0.0f, 1.0f, 0.0f, 0.5f});
services.canvas.draw_circle({300.0f, 200.0f}, 40.0f, {0.2f, 0.6f, 1.0f, 1.0f});
```

---

## Next Steps

- Set up camera views in [Camera](Camera.md).
- Create in-game interfaces in [UI Basics](../ui/UI-Basics.md).
