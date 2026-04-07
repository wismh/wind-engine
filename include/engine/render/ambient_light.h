#pragma once

// docs/tech/modules/Render.md (Lights)

#include <glm/vec3.hpp>

namespace engine::render {

// Light that reaches every surface equally, whatever its normal. The scene's ambient term is the sum of
// `color * intensity` over every `AmbientLight`; the entity's transform does not matter.
struct AmbientLight {
    glm::vec3 color{1.0f};
    float intensity = 0.2f;
};

}
