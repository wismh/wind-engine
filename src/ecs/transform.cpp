#include <engine/ecs/transform.h>

#include <engine/core/det_math.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace engine {

glm::mat4 model_matrix(const Transform& transform) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), transform.position);
    model *= glm::mat4_cast(transform.rotation);
    return glm::scale(model, transform.scale);
}

glm::quat rotation_z(float radians) {
    const SinCos half = det_sin_cos(radians * 0.5f);
    return glm::quat{half.cos, 0.0f, 0.0f, half.sin};
}

glm::quat euler_xyz(glm::vec3 radians) {
    const SinCos x = det_sin_cos(radians.x * 0.5f);
    const SinCos y = det_sin_cos(radians.y * 0.5f);
    const SinCos z = det_sin_cos(radians.z * 0.5f);
    return glm::quat{x.cos, x.sin, 0.0f, 0.0f} * glm::quat{y.cos, 0.0f, y.sin, 0.0f} *
           glm::quat{z.cos, 0.0f, 0.0f, z.sin};
}

glm::quat angle_axis(float radians, glm::vec3 axis) {
    const SinCos half = det_sin_cos(radians * 0.5f);
    return glm::quat{half.cos, axis * half.sin};
}

float angle_z(const glm::quat& rotation) {
    const glm::vec3 x_axis = rotation * glm::vec3{1.0f, 0.0f, 0.0f};
    if (std::abs(x_axis.x) < 1e-6f && std::abs(x_axis.y) < 1e-6f) {
        return 0.0f;
    }
    return det_atan2(x_axis.y, x_axis.x);
}

}
