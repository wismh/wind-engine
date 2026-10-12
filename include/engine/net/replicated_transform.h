#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace engine::net {

struct ReplicatedTransform {
    glm::vec3 target_position{0.0f};
    glm::quat target_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float lerp_speed{15.0f};
};

} // namespace engine::net
