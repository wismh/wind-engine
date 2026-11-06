#pragma once

// Private. Games read CollisionEvent3D in <engine/ecs/physics_3d.h>. This header is not installed.

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace engine {

enum class ContactShapeKind3D : std::uint8_t { Box, Sphere, Capsule };

// World-space shape. Scale is already excluded. A box uses `rotation` and `half_extents` (full size / 2).
// A sphere ignores rotation. A capsule's segment runs along `rotation * +Y` for `height` (center to center).
struct ContactShape3D {
    ContactShapeKind3D kind{ContactShapeKind3D::Sphere};
    glm::vec3 center{0.f};
    glm::quat rotation{1.f, 0.f, 0.f, 0.f};
    glm::vec3 half_extents{0.f};
    float radius{0.f};
    float height{0.f};
};

struct ContactPoint3D {
    glm::vec3 point{0.f};
    float penetration{0.f};
};

// v1 keeps one point, the deepest. count == 0 is separation.
inline constexpr int kContact3DPointCapacity = 1;

struct ContactManifold3D {
    glm::vec3 normal{0.f};  // unit vector from the first shape toward the second when count > 0
    int count{0};
    ContactPoint3D points[kContact3DPointCapacity]{};
};

// No World. A penetration of 0 is a touch. A non-finite component, a negative half extent, a negative
// radius, or a negative capsule height returns an empty manifold. Coincident centers use +X.
[[nodiscard]] ContactManifold3D make_contact_3d(const ContactShape3D& a, const ContactShape3D& b);

[[nodiscard]] ContactManifold3D contact_box_box_3d(const ContactShape3D& a, const ContactShape3D& b);

// `capsule_is_a` selects the manifold normal's direction (from the first shape toward the second).
[[nodiscard]] ContactManifold3D contact_capsule_box_3d(const ContactShape3D& capsule, const ContactShape3D& box,
                                                       bool capsule_is_a);

}
