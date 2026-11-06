#pragma once

// docs/tech/modules/Render.md (Lights)

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstddef>

namespace engine::render {

// Directional lights reserved at the front of one view's light list. Point and spot lights fill what remains of
// `kMaxForwardLights`. `builtin_assets/shaders/.forward_light.glsl` and `.forward_light.slang` size their arrays with
// `kMaxForwardLights`; change the constant and both snippets together.
inline constexpr std::size_t kMaxDirectionalLights = 4;
inline constexpr std::size_t kMaxForwardLights = 16;

// Direction and ambient used when a world has no light component at all: a white light from above, in front and to
// the left, so the faces of a lit cube at the origin seen from +Z all differ.
inline constexpr glm::vec3 kDefaultLightDirection{0.3f, -1.0f, -0.5f};
inline constexpr glm::vec3 kDefaultLightColor{1.0f};
inline constexpr glm::vec3 kDefaultAmbient{0.2f};

// How a record in `SceneLights` is shaded. The shader reads this as the w component of the packed color.
enum class LightKind : int {
    Directional = 0,
    Point = 1,
    Spot = 2,
};

// One analytic light in world space, after gathering and before the shader's packed vec4s.
struct SceneLight {
    LightKind kind = LightKind::Directional;
    // `color * intensity`.
    glm::vec3 color{0.0f};
    // Unit vector the light travels along. Directional and spot. Unused for a point.
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    // World position. Point and spot.
    glm::vec3 position{0.0f};
    // World units. Unused for a directional.
    float range = 0.0f;
    // Cone terms: `saturate(dot(-L, direction) * spot_scale + spot_bias)`. Zero for a point and a directional.
    float spot_scale = 0.0f;
    float spot_bias = 0.0f;

    bool operator==(const SceneLight&) const = default;
};

// The lights of one view, in world space, as `CmdSetView` carries them to the backend. Directional lights occupy
// the front of `lights` (at most `kMaxDirectionalLights`). Point and spot follow, nearest to the camera first.
struct SceneLights {
    std::array<SceneLight, kMaxForwardLights> lights{};
    int count = 0;
    // Sum of `color * intensity` over every ambient light.
    glm::vec3 ambient{0.0f};

    bool operator==(const SceneLights&) const = default;
};

// The four vec4 arrays the lit shaders read. `color_kind.w` is `LightKind`. `direction_range.w` is range.
// `position_spot.w` is `spot_scale`. `spot_bias.x` is `spot_bias`.
struct PackedForwardLights {
    std::array<glm::vec4, kMaxForwardLights> color_kind{};
    std::array<glm::vec4, kMaxForwardLights> direction_range{};
    std::array<glm::vec4, kMaxForwardLights> position_spot{};
    std::array<glm::vec4, kMaxForwardLights> spot_bias{};
    int count = 0;
    glm::vec3 ambient{0.0f};
};

[[nodiscard]] inline PackedForwardLights pack_forward_lights(const SceneLights& lights) {
    PackedForwardLights packed;
    packed.count = lights.count;
    packed.ambient = lights.ambient;
    const int count = lights.count < 0 ? 0 : lights.count;
    const int n = count > static_cast<int>(kMaxForwardLights) ? static_cast<int>(kMaxForwardLights) : count;
    for (int i = 0; i < n; ++i) {
        const SceneLight& light = lights.lights[static_cast<std::size_t>(i)];
        packed.color_kind[static_cast<std::size_t>(i)] =
                glm::vec4{light.color, static_cast<float>(static_cast<int>(light.kind))};
        packed.direction_range[static_cast<std::size_t>(i)] = glm::vec4{light.direction, light.range};
        packed.position_spot[static_cast<std::size_t>(i)] = glm::vec4{light.position, light.spot_scale};
        packed.spot_bias[static_cast<std::size_t>(i)] = glm::vec4{light.spot_bias, 0.0f, 0.0f, 0.0f};
    }
    return packed;
}

}
