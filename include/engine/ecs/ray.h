#pragma once

// docs/tech/modules/ECS.md (Camera)

#include <glm/vec3.hpp>

#include <optional>

namespace engine {

// A half-line in world space: `point = origin + t * direction`, `t >= 0`. `direction` is unit length when the ray
// comes from `screen_to_ray`, and then `t` is a distance. The query functions below also accept a non-unit direction;
// `t` stays a multiple of that vector, except `ray_segment`, which normalizes it.
struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};

// One intersection. `normal` is unit length and faces back along the ray (`dot(normal, direction) <= 0`).
struct RayHit {
    float t = 0.0f;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f};
};

// The infinite line through `ray` against the plane through `point` with `normal` (any non-zero length). `t` may be
// negative when the plane lies behind the origin. nullopt when the line is parallel to the plane or `normal` is zero.
[[nodiscard]] std::optional<RayHit> line_plane(const Ray& ray, glm::vec3 point, glm::vec3 normal);

// `line_plane` kept only when `t >= 0`.
[[nodiscard]] std::optional<RayHit> ray_plane(const Ray& ray, glm::vec3 point, glm::vec3 normal);

// The near surface of the sphere. An origin inside the sphere reports `t = 0` at the origin. `radius < 0` misses.
[[nodiscard]] std::optional<RayHit> ray_sphere(const Ray& ray, glm::vec3 center, float radius);

// The slab entry of an axis-aligned box in the same space as `ray`. A reversed bound is swapped. An origin inside the
// box reports `t = 0` and a normal of `-normalize(direction)`.
[[nodiscard]] std::optional<RayHit> ray_aabb(const Ray& ray, glm::vec3 box_min, glm::vec3 box_max);

// Either side of the triangle. Corners are wound in either order.
[[nodiscard]] std::optional<RayHit> ray_triangle(const Ray& ray, glm::vec3 a, glm::vec3 b, glm::vec3 c);

// The quad as triangles ABC and ACD. `a`, `b`, `c`, `d` go around the boundary. The nearer hit wins.
[[nodiscard]] std::optional<RayHit> ray_quad(const Ray& ray, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d);

// The half-ray comes within `radius` of the segment that starts at `start` and runs `length` world units along
// `direction`. `t` is the closest point on the normalized ray, not the surface entry. `length < 0` or `radius < 0`
// misses.
[[nodiscard]] std::optional<RayHit> ray_segment(
        const Ray& ray, glm::vec3 start, glm::vec3 direction, float length, float radius);

}
