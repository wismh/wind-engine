#pragma once

#include <engine/ecs/entity.h>
#include <engine/ecs/transform.h>
#include <engine/ui/canvas.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace engine {

struct Camera {
    float ortho_size = 10.f;  // half-height of the ortho volume (world units)
    bool auto_aspect = true;  // default; rebuild aspect from WindowSize
    float aspect = 16.f / 9.f;
    float near_clip = -1.f;
    float far_clip = 1.f;
};

struct ActiveCamera {
    ecs::Entity entity{};
};

[[nodiscard]] float camera_aspect(const Camera& camera, const ui::WindowSize& window);

[[nodiscard]] glm::mat4 view_matrix(const Transform& transform);
[[nodiscard]] glm::mat4 projection_matrix(const Camera& camera, const ui::WindowSize& window);

[[nodiscard]] glm::vec3 screen_to_world(
        glm::vec2 screen, const Camera& camera, const Transform& transform, const ui::WindowSize& window);
[[nodiscard]] glm::vec2 world_to_screen(
        glm::vec3 world, const Camera& camera, const Transform& transform, const ui::WindowSize& window);

}
