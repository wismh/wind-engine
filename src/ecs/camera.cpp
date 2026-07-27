#include <engine/ecs/camera.h>

#include <engine/log.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace engine {

namespace {

bool warned_perspective_clip = false;

glm::vec4 viewport_of(const ui::WindowSize& window) {
    return {0.0f, 0.0f, static_cast<float>(window.width), static_cast<float>(window.height)};
}

// Screen pixels (top-left origin) to GL window coordinates (bottom-left origin) at depth `depth` (0 near, 1 far).
glm::vec3 window_point(glm::vec2 screen, const ui::WindowSize& window, float depth) {
    return {screen.x, static_cast<float>(window.height) - screen.y, depth};
}

}

float camera_aspect(const Camera& camera, const ui::WindowSize& window) {
    if (camera.auto_aspect && window.height != 0) {
        return static_cast<float>(window.width) / static_cast<float>(window.height);
    }
    return camera.aspect;
}

glm::mat4 view_matrix(const glm::mat4& camera_world) {
    return glm::inverse(camera_world);
}

glm::mat4 projection_matrix(const Camera& camera, const ui::WindowSize& window) {
    const float aspect = camera_aspect(camera, window);
    if (camera.projection == Projection::Perspective) {
        const float near_clip = std::max(camera.near_clip, kMinPerspectiveNear);
        const float far_clip = std::max(camera.far_clip, near_clip + kMinPerspectiveNear);
        if ((near_clip != camera.near_clip || far_clip != camera.far_clip) && !warned_perspective_clip) {
            warned_perspective_clip = true;
            log::warn("Camera: a perspective camera needs 0 < near_clip < far_clip; clamped (warned once)");
        }
        return glm::perspective(camera.fov_y, aspect, near_clip, far_clip);
    }
    const float half_height = camera.ortho_size;
    const float half_width = half_height * aspect;
    return glm::ortho(-half_width, half_width, -half_height, half_height, camera.near_clip, camera.far_clip);
}

Ray screen_to_ray(glm::vec2 screen, const Camera& camera, const glm::mat4& camera_world, const ui::WindowSize& window) {
    const glm::mat4 view = view_matrix(camera_world);
    const glm::mat4 proj = projection_matrix(camera, window);
    const glm::vec4 viewport = viewport_of(window);
    const glm::vec3 near_point = glm::unProject(window_point(screen, window, 0.0f), view, proj, viewport);
    const glm::vec3 far_point = glm::unProject(window_point(screen, window, 1.0f), view, proj, viewport);
    return Ray{near_point, glm::normalize(far_point - near_point)};
}

glm::vec3 screen_to_world(
        glm::vec2 screen, const Camera& camera, const glm::mat4& camera_world, const ui::WindowSize& window) {
    if (camera.projection == Projection::Orthographic) {
        const glm::mat4 view = view_matrix(camera_world);
        const glm::mat4 proj = projection_matrix(camera, window);
        return glm::unProject(window_point(screen, window, 0.5f), view, proj, viewport_of(window));
    }
    const Ray ray = screen_to_ray(screen, camera, camera_world, window);
    if (const std::optional<RayHit> hit = ray_plane(ray, glm::vec3{0.0f}, glm::vec3{0.0f, 0.0f, 1.0f})) {
        return hit->point;
    }
    return ray.origin;
}

glm::vec2 world_to_screen(
        glm::vec3 world, const Camera& camera, const glm::mat4& camera_world, const ui::WindowSize& window) {
    const glm::mat4 view = view_matrix(camera_world);
    const glm::mat4 proj = projection_matrix(camera, window);
    const glm::vec3 win = glm::project(world, view, proj, viewport_of(window));
    return {win.x, static_cast<float>(window.height) - win.y};
}

}
