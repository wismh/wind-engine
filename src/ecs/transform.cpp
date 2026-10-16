#include <engine/ecs/transform.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace engine {

glm::mat4 model_matrix(const Transform& transform) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), transform.position);
    model *= glm::mat4_cast(transform.rotation);
    return glm::scale(model, transform.scale);
}

glm::quat rotation_z(float radians) {
    return glm::angleAxis(radians, glm::vec3{0.0f, 0.0f, 1.0f});
}

glm::quat euler_xyz(glm::vec3 radians) {
    return glm::angleAxis(radians.x, glm::vec3{1.0f, 0.0f, 0.0f}) *
           glm::angleAxis(radians.y, glm::vec3{0.0f, 1.0f, 0.0f}) *
           glm::angleAxis(radians.z, glm::vec3{0.0f, 0.0f, 1.0f});
}

float angle_z(const glm::quat& rotation) {
    const glm::vec3 x_axis = rotation * glm::vec3{1.0f, 0.0f, 0.0f};
    if (std::abs(x_axis.x) < 1e-6f && std::abs(x_axis.y) < 1e-6f) {
        return 0.0f;
    }
    return std::atan2(x_axis.y, x_axis.x);
}

}
