#pragma once

// Private. Inertia of one collider about the body origin. Not installed.

#include <glm/vec2.hpp>

namespace engine {

// Box: mass * (width^2 + height^2) / 12, full extents. Circle: 0.5 * mass * radius^2.
// Polygon: second moment of the vertex ring about the origin of that list (not recentered).
// Returns 0 when the shape is degenerate, not CCW-convex, or mass is not positive.
[[nodiscard]] float inertia_2d_box(float mass, float width, float height);

[[nodiscard]] float inertia_2d_circle(float mass, float radius);

[[nodiscard]] float inertia_2d_polygon(float mass, const glm::vec2* vertices, int count);

}
