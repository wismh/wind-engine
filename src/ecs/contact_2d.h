#pragma once

// Private. Games read CollisionEvent2D in <engine/ecs/physics_2d.h>. This header is not installed.

#include <glm/vec2.hpp>

#include <cstdint>

namespace engine {

class SdfField2D;

enum class ContactShapeKind : std::uint8_t { Aabb, Circle, Polygon, Sdf };

// Matches PolygonCollider2D's vertex cap.
inline constexpr int kContactPolygonCapacity = 8;

// XY shape in world space. An AABB with angle exactly 0 uses the legacy overlap.
// A nonzero angle turns a box into four corners. Polygon vertices are world-space and CCW.
// Circles ignore angle. half_extents is full box size / 2.
// Sdf is one chunk of an SdfCollider2D: `sdf` in the frame at `center` turned by `angle`; only body samples that
// fall in chunk `sdf_chunk` of the field count (contact_2d_sdf.cpp).
struct ContactShape2D {
    ContactShapeKind kind{ContactShapeKind::Aabb};
    glm::vec2 center{0.f};
    glm::vec2 half_extents{0.f};
    float radius{0.f};
    float angle{0.f};
    glm::vec2 vertices[kContactPolygonCapacity]{};
    int vertex_count{0};
    const SdfField2D* sdf{nullptr};
    glm::ivec2 sdf_chunk{0};
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
// convex CCW ring returns an empty manifold. Two Sdf shapes return an empty manifold.
[[nodiscard]] ContactManifold2D make_contact_2d(const ContactShape2D& a, const ContactShape2D& b);

// Polygon-polygon and polygon-circle, including boxes whose angle is not exactly 0.
// Defined in contact_2d_polygon.cpp.
[[nodiscard]] ContactManifold2D contact_convex_2d(const ContactShape2D& a, const ContactShape2D& b);

// A circle, box or polygon (`body`) against one SDF chunk (`field`). The normal points from `body` toward the field
// (into the solid). Defined in contact_2d_sdf.cpp.
[[nodiscard]] ContactManifold2D contact_sdf_2d(const ContactShape2D& body, const ContactShape2D& field);

}
