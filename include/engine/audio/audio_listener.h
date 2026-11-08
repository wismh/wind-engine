#pragma once

// docs/tech/modules/Audio.md

#include <glm/vec3.hpp>

namespace engine {

struct AudioListener {
    bool active = true;
    glm::vec3 velocity{0.f, 0.f, 0.f};
};

} // namespace engine
