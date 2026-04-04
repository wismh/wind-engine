#pragma once

// docs/tech/modules/Render.md (Lights)

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>

namespace engine::render {

// The most directional lights one view carries. `builtin_assets/shaders/lit.shader` declares its arrays with the
// same size (`kMaxDirectionalLights` there); change both together.
inline constexpr std::size_t kMaxDirectionalLights = 4;

// Direction and ambient used when a world has no `DirectionalLight` and no `AmbientLight` at all: a white light from
// above, in front and to the left, so the faces of a lit cube at the origin seen from +Z all differ.
inline constexpr glm::vec3 kDefaultLightDirection{0.3f, -1.0f, -0.5f};
inline constexpr glm::vec3 kDefaultLightColor{1.0f};
inline constexpr glm::vec3 kDefaultAmbient{0.2f};

// The lights of one view, in world space, as `CmdSetView` carries them to the backend.
struct SceneLights {
    // Unit vectors the light travels along (the light's forward). Only the first `directional_count` are used.
    std::array<glm::vec3, kMaxDirectionalLights> directions{};
    // `color * intensity` of each directional light.
    std::array<glm::vec3, kMaxDirectionalLights> colors{};
    int directional_count = 0;
    // Sum of `color * intensity` over every ambient light.
    glm::vec3 ambient{0.0f};

    bool operator==(const SceneLights&) const = default;
};

}
