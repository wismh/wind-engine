#pragma once

#include <engine/render/particles.h>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>

namespace engine::render {

// |n.y| at or above this uses +X as the disc helper. +Y is the default direction and the zero fallback, and
// cross(+Y, n) is zero when n is parallel to Y.
inline constexpr float kDiscNormalYLimit = 0.9f;

// Same squared-length cutoff `compute_direction` uses before it substitutes +Y. That function is not changed.
inline constexpr float kEmitterDirectionEpsilon = 1e-6f;

// Sphere: `u`, `v`, `w` in [0, 1]. `cbrt(u)` is the radius, so the sample is uniform in the volume of the
// ellipsoid whose semi-axes are `scale`, not on its surface. Disc: `u` is the area radius (`sqrt(u)`), `v` is the
// angle fraction (`v * two_pi`), `w` is unused. The disc lies in the plane of `direction` (or +Y when that vector is
// near zero) with semi-axes `scale.x` and `scale.y`. Circle is not sampled here.
[[nodiscard]] inline glm::vec3 particle_shape_offset(
        EmitterShape shape, glm::vec3 scale, glm::vec3 direction, float u, float v, float w) noexcept {
    switch (shape) {
        case EmitterShape::Sphere: {
            const float radius = std::cbrt(u);
            const float z = 2.0f * v - 1.0f;
            const float phi = w * glm::two_pi<float>();
            const float xy = std::sqrt(std::max(0.0f, 1.0f - z * z));
            const glm::vec3 unit{xy * std::cos(phi) * radius, xy * std::sin(phi) * radius, z * radius};
            return {unit.x * scale.x, unit.y * scale.y, unit.z * scale.z};
        }
        case EmitterShape::Disc: {
            glm::vec3 normal = direction;
            if (glm::dot(normal, normal) < kEmitterDirectionEpsilon) {
                normal = {0.0f, 1.0f, 0.0f};
            } else {
                normal = glm::normalize(normal);
            }
            const glm::vec3 helper = std::abs(normal.y) < kDiscNormalYLimit ? glm::vec3{0.0f, 1.0f, 0.0f}
                                                                             : glm::vec3{1.0f, 0.0f, 0.0f};
            const glm::vec3 tangent = glm::normalize(glm::cross(helper, normal));
            const glm::vec3 bitangent = glm::cross(normal, tangent);
            const float rho = std::sqrt(u);
            const float theta = v * glm::two_pi<float>();
            return tangent * (std::cos(theta) * rho * scale.x) + bitangent * (std::sin(theta) * rho * scale.y);
        }
        default:
            return {0.0f, 0.0f, 0.0f};
    }
}

}
