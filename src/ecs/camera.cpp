#include <engine/ecs/camera.h>

#include <glm/gtc/matrix_transform.hpp>

namespace engine {

float camera_aspect(const Camera& camera, const ui::WindowSize& window) {
    if (camera.auto_aspect && window.height != 0) {
        return static_cast<float>(window.width) / static_cast<float>(window.height);
    }
    return camera.aspect;
}

glm::mat4 view_matrix(const Transform& transform) {
    return glm::translate(glm::mat4(1.0f), -transform.position);
}

glm::mat4 projection_matrix(const Camera& camera, const ui::WindowSize& window) {
    const float aspect = camera_aspect(camera, window);
    const float half_height = camera.ortho_size;
    const float half_width = half_height * aspect;
    return glm::ortho(-half_width, half_width, -half_height, half_height, camera.near_clip, camera.far_clip);
}

glm::vec3 screen_to_world(
        glm::vec2 screen, const Camera& camera, const Transform& transform, const ui::WindowSize& window) {
    const glm::mat4 view = view_matrix(transform);
    const glm::mat4 proj = projection_matrix(camera, window);
    const glm::vec4 viewport{
            0.0f, 0.0f, static_cast<float>(window.width), static_cast<float>(window.height)};
    glm::vec3 win{screen.x, static_cast<float>(window.height) - screen.y, 0.5f};
    return glm::unProject(win, view, proj, viewport);
}

glm::vec2 world_to_screen(
        glm::vec3 world, const Camera& camera, const Transform& transform, const ui::WindowSize& window) {
    const glm::mat4 view = view_matrix(transform);
    const glm::mat4 proj = projection_matrix(camera, window);
    const glm::vec4 viewport{
            0.0f, 0.0f, static_cast<float>(window.width), static_cast<float>(window.height)};
    glm::vec3 win = glm::project(world, view, proj, viewport);
    win.y = static_cast<float>(window.height) - win.y;
    return {win.x, win.y};
}

}
