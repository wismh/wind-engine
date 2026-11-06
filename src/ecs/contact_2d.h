#pragma once

// Private. Games read CollisionEvent2D in <engine/ecs/physics_2d.h>. This header is not installed.

#include <glm/vec2.hpp>

#include <cstdint>

namespace engine {

enum class ContactShapeKind : std::uint8_t { Aabb, Circle };

// XY shape in world space. Boxes are axis-aligned; half_extents is full size / 2. Circles use radius.
struct ContactShape2D {
    ContactShapeKind kind{ContactShapeKind::Aabb};
    glm::vec2 center{0.f};
    glm::vec2 half_extents{0.f};
    float radius{0.f};
};

struct ContactPoint2D {
    glm::vec2 point{0.f};
    float penetration{0.f};
};

// Stage 1 writes one point. points[0] is the sample CollisionEvent2D publishes. A later stage may set count to 2.
// points[0] stays the deeper sample; equal depth keeps the stage-1 point in points[0].
inline constexpr int kContact2DPointCapacity = 2;

struct ContactManifold2D {
    glm::vec2 normal{0.f};  // unit XY from the first shape toward the second when count > 0
    int count{0};
    ContactPoint2D points[kContact2DPointCapacity]{};
};

// No World. count == 0 is separation (no event). A penetration of 0 is a touch and still a hit.
// A non-finite component, a negative half extent, or a negative radius returns an empty manifold.
[[nodiscard]] ContactManifold2D make_contact_2d(const ContactShape2D& a, const ContactShape2D& b);

}
