#pragma once

// docs/tech/modules/Render.md (Lights)

#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

namespace engine::render {

// A cone at the entity's world position, aimed along world forward (the -Z column of `ecs::world_matrix`), the same
// rule as `DirectionalLight`. `inner_angle` and `outer_angle` are radians from that axis. An inner angle greater
// than or equal to the outer angle is a hard edge at the outer angle. A flashlight is a `SpotLight`.
struct SpotLight {
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
    float inner_angle = glm::radians(25.0f);
    float outer_angle = glm::radians(35.0f);
};

}
