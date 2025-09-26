# Camera

Wind includes an orthographic 2D camera (`engine::Camera`) with coordinate transformations between window pixels and world units. The camera is a component on an entity together with an `engine::Transform`, and one entity is marked as the active camera.

---

## 1. Creating a Camera Entity

```cpp
#include <engine/ecs/camera.h>
#include <engine/ecs/transform.h>

engine::ecs::Entity cam = world.create();
world.emplace<engine::Transform>(cam, engine::Transform{.position = {0.0f, 0.0f, 0.0f}});   // camera focus at (0, 0)
world.emplace<engine::Camera>(cam, engine::Camera{.ortho_size = 5.0f});

// The world draws through this entity
world.ctx<engine::ActiveCamera>().entity = cam;
```

- `ortho_size` is the **half-height** of the view in world units (default `10`). A window 720 pixels tall with `ortho_size = 5` shows 10 world units from bottom to top. The width follows the window's aspect ratio (`auto_aspect`, the default).
- `near_clip` / `far_clip` default to `-1` / `1`, so sprites at `z = 0` are visible.
- Without a valid `ActiveCamera` the engine draws no world sprites or meshes (UI still draws). An `ActiveCamera` pointing at a live entity that lacks `Camera` or `Transform` is a fatal error.
- The view is the camera's `Transform::position` only: rotation and scale of the camera entity are not applied. The world is drawn into every window bound to the world, each with its own aspect ratio.

---

## 2. Coordinate Conversions

`screen_to_world` and `world_to_screen` are free functions. They take the camera, its transform, and the size of the window, which you read with `ui::window_size_for`:

```cpp
#include <engine/ecs/camera.h>
#include <engine/ui/canvas.h>

const auto& camera = world.get<engine::Camera>(cam);
const auto& cam_transform = world.get<engine::Transform>(cam);
const engine::ui::WindowSize window = engine::ui::window_size_for(world, engine::kPrimaryWindow);

// Window pixel -> world coordinates (the plane halfway between near_clip and far_clip, z = 0 here)
glm::vec2 mouse_screen_pos = {640.0f, 360.0f};   // e.g. MouseEvent::position
glm::vec3 mouse_world_pos = engine::screen_to_world(mouse_screen_pos, camera, cam_transform, window);

// World coordinates -> window pixel
glm::vec3 enemy_world_pos = {2.5f, -1.0f, 0.0f};
glm::vec2 enemy_screen_pos = engine::world_to_screen(enemy_world_pos, camera, cam_transform, window);
```

`view_matrix(transform)`, `projection_matrix(camera, window)`, and `camera_aspect(camera, window)` give the matrices the engine itself uses.

---

## 3. Smooth Camera Tracking

A typical camera follow system interpolates the camera's position towards the player. Run it on `Schedule::Frame`, `Phase::Game`:

```cpp
#include <glm/common.hpp>

#include <cmath>

void camera_tracking_system(engine::ecs::World& world) {
    const auto& time = world.ctx<engine::Time>();

    // Locate the player position
    glm::vec3 player_pos{0.0f};
    auto players = world.view<engine::Transform, PlayerTag>();
    for (engine::ecs::Entity e : players) {
        player_pos = players.get<engine::Transform>(e).position;
        break;
    }

    // Smoothly move the camera towards it
    const float blend = 1.0f - std::exp(-8.0f * time.delta_time);
    const engine::ecs::Entity cam = world.ctx<engine::ActiveCamera>().entity;
    auto& cam_transform = world.get<engine::Transform>(cam);
    cam_transform.position = glm::mix(cam_transform.position, player_pos, blend);
}
```

---

## Next Steps

- Design user interfaces with [UI Basics](../ui/UI-Basics.md).
- Connect game controls to actions with [Action Mapping](../input/Action-Mapping.md).
