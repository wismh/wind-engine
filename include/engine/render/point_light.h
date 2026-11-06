#pragma once

// docs/tech/modules/Render.md (Lights)

#include <glm/vec3.hpp>

namespace engine::render {

// A light at the entity's world position. Range is in world units. The transform's rotation does not aim it.
// An entity without `Transform` sits at the origin. `run_render` keeps the nearest ones that meet the view, up to
// the room left in `kMaxForwardLights` after directional lights.
struct PointLight {
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
};

}
