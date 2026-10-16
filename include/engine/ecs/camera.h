#pragma once

// docs/tech/modules/ECS.md

#include <engine/ecs/entity.h>
#include <engine/ecs/ray.h>
#include <engine/ecs/transform.h>
#include <engine/ui/canvas.h>

#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace engine {

enum class Projection {
    Orthographic,
    Perspective,
};

// Perspective clamps `near_clip` up to this, and `far_clip` up to `near + kMinPerspectiveNear`.
inline constexpr float kMinPerspectiveNear = 0.01f;

// The camera looks down its local -Z axis with +Y up. The defaults are the 2D camera: orthographic, near -1, far 1.
struct Camera {
    Projection projection = Projection::Orthographic;
    float ortho_size = 10.f;           // orthographic: half-height of the view volume (world units)
    float fov_y = glm::radians(60.f);  // perspective: vertical field of view (radians)
    bool auto_aspect = true;           // default; rebuild aspect from WindowSize
    float aspect = 16.f / 9.f;
    // Orthographic: any range. Perspective needs 0 < near < far (for example 0.1 and 1000); a smaller near is
    // clamped to kMinPerspectiveNear with a warning logged once.
    float near_clip = -1.f;
    float far_clip = 1.f;
};

struct ActiveCamera {
    ecs::Entity entity{};
};

[[nodiscard]] float camera_aspect(const Camera& camera, const ui::WindowSize& window);

// `camera_world` is the camera entity's world matrix (`ecs::world_matrix`). The view is its inverse, so a rotated
// or parented camera works.
[[nodiscard]] glm::mat4 view_matrix(const glm::mat4& camera_world);
// glm::ortho from `ortho_size` or glm::perspective from `fov_y`, both with `camera_aspect`.
[[nodiscard]] glm::mat4 projection_matrix(const Camera& camera, const ui::WindowSize& window);

// `screen` is in window pixels, origin top-left, +Y down. The ray starts on the near plane and points away from the
// camera: along the camera's forward (-Z) for an orthographic camera, through the pixel for a perspective one.
[[nodiscard]] Ray screen_to_ray(
        glm::vec2 screen, const Camera& camera, const glm::mat4& camera_world, const ui::WindowSize& window);

// Orthographic: the point under `screen` halfway through the clip range (z of the camera for the default near -1,
// far 1). Perspective: where `screen_to_ray` meets the world plane z = 0; the ray's near-plane origin when the ray is
// parallel to that plane or the plane is behind the camera.
[[nodiscard]] glm::vec3 screen_to_world(
        glm::vec2 screen, const Camera& camera, const glm::mat4& camera_world, const ui::WindowSize& window);
// The pixel `world` projects to (origin top-left, +Y down). Either projection.
[[nodiscard]] glm::vec2 world_to_screen(
        glm::vec3 world, const Camera& camera, const glm::mat4& camera_world, const ui::WindowSize& window);

}
