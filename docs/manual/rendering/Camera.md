# Camera

Wind includes an orthographic 2D camera system (`engine::ecs::Camera`) with coordinate transformations between screen pixel coordinates and world units.

---

## 1. Creating a Camera Entity

Spawn an entity with an `engine::ecs::Camera` and an `engine::ecs::Transform`:

```cpp
#include <engine/ecs/camera.h>
#include <engine/ecs/transform.h>

auto cam = world.create();
world.emplace<engine::ecs::Transform>(cam, glm::vec2{0.0f, 0.0f}); // Camera focus at (0, 0)
world.emplace<engine::ecs::Camera>(cam, engine::ecs::Camera{
    .viewport_size = {1280.0f, 720.0f},
    .zoom = 1.0f
});
```

---

## 2. Coordinate Conversions

The free functions in `<engine/ecs/camera.h>` translate between screen space (mouse position, window pixels) and world space. They take the camera's **world matrix**, so a rotated camera or a camera attached to another entity works:

```cpp
#include <engine/ecs/hierarchy.h>

const auto& camera = world.get<engine::Camera>(cam);
const glm::mat4 camera_world = engine::ecs::world_matrix(world, cam);
const engine::ui::WindowSize window = engine::ui::window_size_for(world, engine::kPrimaryWindow);

// Screen pixel -> World coordinates
glm::vec3 mouse_world = engine::screen_to_world({640.0f, 360.0f}, camera, camera_world, window);

// World coordinates -> Screen pixel
glm::vec2 enemy_screen = engine::world_to_screen({250.0f, -100.0f, 0.0f}, camera, camera_world, window);
```

`view_matrix(camera_world)` is the inverse of that matrix. The engine refreshes `world_matrix` in `Phase::Transform`; a system in `Phase::Game` that moved the camera's parent this frame should call `engine::ecs::compute_world_matrix` instead.

### Camera on a moving rig

Make the camera a child of the entity it follows (`engine::ecs::set_parent(world, cam, rig)`, see [Hierarchy](../ecs/Hierarchy.md)). Its local `Transform` is then an offset from the rig and the view follows the rig, including its rotation.

---

## 3. Smooth Camera Tracking

A typical camera follow system interpolates the camera's position towards the player:

```cpp
void camera_tracking_system(engine::ecs::World& world) {
    const auto& time = world.ctx<engine::Time>();

    // Locate player position
    glm::vec2 player_pos{0.0f, 0.0f};
    for (auto [e, pos, tag] : world.view<engine::ecs::Transform, PlayerTag>().each()) {
        player_pos = pos.position;
        break;
    }

    // Smoothly track with camera
    for (auto [e, cam_pos, cam] : world.view<engine::ecs::Transform, engine::ecs::Camera>().each()) {
        float blend = 1.0f - std::exp(-8.0f * time.delta_time);
        cam_pos.position = glm::mix(cam_pos.position, player_pos, blend);
    }
}
```

---

## Next Steps

- Design user interfaces with [UI Basics](../ui/UI-Basics.md).
- Connect game controls to actions with [Action Mapping](../input/Action-Mapping.md).
