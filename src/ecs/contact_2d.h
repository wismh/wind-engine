#pragma once

// Private. Games read CollisionEvent2D in <engine/ecs/physics_2d.h>. This header is not installed.

#include <glm/vec2.hpp>

#include <cstdint>

namespace engine {

enum class ContactShapeKind : std::uint8_t { Aabb, Circle, Polygon };

// Matches PolygonCollider2D's vertex cap.
inline constexpr int kContactPolygonCapacity = 8;

// XY shape in world space. An AABB with angle exactly 0 uses the legacy overlap.
// A nonzero angle turns a box into four corners. Polygon vertices are world-space and CCW.
// Circles ignore angle. half_extents is full box size / 2.
struct ContactShape2D {
    ContactShapeKind kind{ContactShapeKind::Aabb};
    glm::vec2 center{0.f};
    glm::vec2 half_extents{0.f};
    float radius{0.f};
    float angle{0.f};
    glm::vec2 vertices[kContactPolygonCapacity]{};
    int vertex_count{0};
};

struct ContactPoint2D {
    glm::vec2 point{0.f};
    float penetration{0.f};
};

// points[0] is the sample CollisionEvent2D publishes: the deeper point, or the contact-segment
// midpoint when the two clip depths are equal. The solver may use both points.
inline constexpr int kContact2DPointCapacity = 2;

struct ContactManifold2D {
    glm::vec2 normal{0.f};  // unit XY from the first shape toward the second when count > 0
    int count{0};
    ContactPoint2D points[kContact2DPointCapacity]{};
};

// No World. count == 0 is separation (no event). A penetration of 0 is a touch and still a hit.
// A non-finite component, a negative half extent, a negative radius, or a polygon that is not a
// convex CCW ring returns an empty manifold.
[[nodiscard]] ContactManifold2D make_contact_2d(const ContactShape2D& a, const ContactShape2D& b);

// Polygon-polygon and polygon-circle, including boxes whose angle is not exactly 0.
// Defined in contact_2d_polygon.cpp.
[[nodiscard]] ContactManifold2D contact_convex_2d(const ContactShape2D& a, const ContactShape2D& b);

}
