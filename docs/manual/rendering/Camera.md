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

The `Camera` component provides methods to translate coordinates between screen space (mouse position, window pixels) and game world space:

```cpp
const auto& camera = world.get<engine::ecs::Camera>(cam);
const auto& cam_transform = world.get<engine::ecs::Transform>(cam);

// Screen pixel -> World coordinates
glm::vec2 mouse_screen_pos = {640.0f, 360.0f};
glm::vec2 mouse_world_pos = camera.screen_to_world(mouse_screen_pos, cam_transform.position);

// World coordinates -> Screen pixel
glm::vec2 enemy_world_pos = {250.0f, -100.0f};
glm::vec2 enemy_screen_pos = camera.world_to_screen(enemy_world_pos, cam_transform.position);
```

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
