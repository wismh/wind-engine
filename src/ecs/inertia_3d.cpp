#include "inertia_3d.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <numbers>

namespace engine {
namespace {

[[nodiscard]] bool positive_mass(float mass) {
    return std::isfinite(mass) && mass > 0.f;
}

}

glm::vec3 inertia_3d_box(float mass, glm::vec3 size) {
    if (!positive_mass(mass) || !std::isfinite(size.x) || !std::isfinite(size.y) || !std::isfinite(size.z) ||
        size.x < 0.f || size.y < 0.f || size.z < 0.f) {
        return glm::vec3{0.f};
    }
    const float x2 = size.x * size.x;
    const float y2 = size.y * size.y;
    const float z2 = size.z * size.z;
    const float scale = mass / 12.f;
    return glm::vec3{scale * (y2 + z2), scale * (z2 + x2), scale * (x2 + y2)};
}

glm::vec3 inertia_3d_sphere(float mass, float radius) {
    if (!positive_mass(mass) || !std::isfinite(radius) || radius < 0.f) {
        return glm::vec3{0.f};
    }
    const float inertia = 0.4f * mass * radius * radius;
    return glm::vec3{inertia};
}

glm::vec3 inertia_3d_capsule(float mass, float radius, float height) {
    if (!positive_mass(mass) || !std::isfinite(radius) || !std::isfinite(height) || radius <= 0.f || height < 0.f) {
        return glm::vec3{0.f};
    }
    const float pi = std::numbers::pi_v<float>;
    const float cylinder_volume = pi * radius * radius * height;
    const float sphere_volume = (4.f / 3.f) * pi * radius * radius * radius;
    const float volume = cylinder_volume + sphere_volume;
    if (volume <= 0.f) {
        return glm::vec3{0.f};
    }
    const float cylinder_mass = mass * cylinder_volume / volume;
    const float sphere_mass = mass * sphere_volume / volume;
    const float hemisphere_mass = sphere_mass * 0.5f;
    const float radius2 = radius * radius;
    const float axial = 0.5f * cylinder_mass * radius2 + 0.4f * sphere_mass * radius2;
    const float radial = cylinder_mass * (3.f * radius2 + height * height) / 12.f +
                         2.f * (0.4f * hemisphere_mass * radius2 + hemisphere_mass * (height * 0.5f) * (height * 0.5f));
    // Local Y is the capsule axis.
    return glm::vec3{radial, axial, radial};
}

glm::mat3 inverse_inertia_world(glm::quat rotation, glm::vec3 inertia) {
    glm::vec3 inverse{0.f};
    if (inertia.x > 0.f) {
        inverse.x = 1.f / inertia.x;
    }
    if (inertia.y > 0.f) {
        inverse.y = 1.f / inertia.y;
    }
    if (inertia.z > 0.f) {
        inverse.z = 1.f / inertia.z;
    }
    const glm::mat3 basis = glm::mat3_cast(rotation);
    const glm::mat3 diagonal{inverse.x, 0.f, 0.f, 0.f, inverse.y, 0.f, 0.f, 0.f, inverse.z};
    return basis * diagonal * glm::transpose(basis);
}

}
