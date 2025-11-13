#include <engine/ecs/transform.h>

#include <glm/gtc/matrix_transform.hpp>

namespace engine {

glm::mat4 model_matrix(const Transform& transform) {
    glm::mat4 model(1.0f);
    model = glm::translate(model, transform.position);
    model = glm::rotate(model, transform.rotation.x, glm::vec3{1.0f, 0.0f, 0.0f});
    model = glm::rotate(model, transform.rotation.y, glm::vec3{0.0f, 1.0f, 0.0f});
    model = glm::rotate(model, transform.rotation.z, glm::vec3{0.0f, 0.0f, 1.0f});
    model = glm::scale(model, transform.scale);
    return model;
}

}
