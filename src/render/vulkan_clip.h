#pragma once

// docs/tech/modules/Render.md

#include <glm/mat4x4.hpp>

namespace engine::render {

// `projection_matrix` is OpenGL clip space: x and y in [-w, w], z in [-w, w]. Vulkan clips z to
// [0, w] and leaves x, y, and w alone. One left-multiply does that remap
// (`z_vk = 0.5 * z_gl + 0.5 * w`) without touching the CPU matrix picking and culling still use.
[[nodiscard]] inline glm::mat4 vulkan_projection(const glm::mat4& gl_projection) {
    glm::mat4 clip{1.0f};
    clip[2][2] = 0.5f;
    clip[3][2] = 0.5f;
    return clip * gl_projection;
}

} // namespace engine::render
