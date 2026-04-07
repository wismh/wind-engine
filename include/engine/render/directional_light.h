#pragma once

// docs/tech/modules/Render.md (Lights)

#include <glm/vec3.hpp>

namespace engine::render {

// A light from infinitely far away, such as the sun. It shines along the entity's world forward: -Z of
// `ecs::world_matrix`, so `Transform::rotation` aims it and a `Parent` carries it. An entity without `Transform` shines
// along world -Z. Position and scale do not matter. `run_render` uses the first `kMaxDirectionalLights` by entity index.
struct DirectionalLight {
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
};

}
