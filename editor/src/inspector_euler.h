#pragma once

#include <engine/ecs/transform.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>

namespace editor {

// `FieldValue` stores a quaternion as x, y, z, w. `glm::quat`'s constructor is w, x, y, z.
inline glm::quat quat_xyzw(double x, double y, double z, double w) {
    const glm::quat rotation{static_cast<float>(w), static_cast<float>(x), static_cast<float>(y),
            static_cast<float>(z)};
    return glm::normalize(rotation);
}

// Degrees, XYZ, the inverse of `engine::euler_xyz`. Snapped so an identity rotation reads 0, 0, 0.
inline glm::vec3 euler_degrees_xyz(glm::quat rotation) {
    rotation = glm::normalize(rotation);
    const glm::mat3 basis = glm::mat3_cast(rotation);
    const float sine_y = std::clamp(basis[2][0], -1.0f, 1.0f);
    const float radians_y = std::asin(sine_y);
    float radians_x = 0.0f;
    float radians_z = 0.0f;
    if (std::abs(std::cos(radians_y)) > 1.0e-4f) {
        radians_x = std::atan2(-basis[2][1], basis[2][2]);
        radians_z = std::atan2(-basis[1][0], basis[0][0]);
    } else {
        radians_x = std::atan2(basis[2][1], basis[1][1]);
    }
    glm::vec3 degrees = glm::degrees(glm::vec3{radians_x, radians_y, radians_z});
    for (int axis = 0; axis < 3; ++axis) {
        const float snapped = std::round(degrees[axis] * 1000.0f) / 1000.0f;
        degrees[axis] = std::abs(snapped) < 5.0e-4f ? 0.0f : snapped;
    }
    return degrees;
}

}
