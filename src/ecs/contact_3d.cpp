#include "contact_3d.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace engine {
namespace {

[[nodiscard]] bool finite_vec(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool finite_quat(glm::quat value) {
    return std::isfinite(value.w) && std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool supported(const ContactShape3D& shape) {
    if (!finite_vec(shape.center) || !finite_quat(shape.rotation)) {
        return false;
    }
    if (shape.kind == ContactShapeKind3D::Box) {
        return finite_vec(shape.half_extents) && shape.half_extents.x >= 0.f && shape.half_extents.y >= 0.f &&
               shape.half_extents.z >= 0.f;
    }
    if (!std::isfinite(shape.radius) || shape.radius < 0.f) {
        return false;
    }
    if (shape.kind == ContactShapeKind3D::Capsule) {
        return std::isfinite(shape.height) && shape.height >= 0.f;
    }
    return true;
}

[[nodiscard]] ContactManifold3D empty_manifold() {
    return ContactManifold3D{};
}

[[nodiscard]] ContactManifold3D finite_or_fallback(const ContactManifold3D& manifold, const ContactShape3D& a) {
    if (manifold.count == 0) {
        return manifold;
    }
    const bool ok = finite_vec(manifold.normal) && std::isfinite(manifold.points[0].penetration) &&
                    finite_vec(manifold.points[0].point);
    if (ok) {
        return manifold;
    }
    ContactManifold3D fallback;
    fallback.normal = glm::vec3{1.f, 0.f, 0.f};
    fallback.count = 1;
    fallback.points[0].penetration = 0.f;
    fallback.points[0].point = finite_vec(a.center) ? a.center : glm::vec3{0.f};
    return fallback;
}

[[nodiscard]] glm::vec3 unit_or_x(glm::vec3 delta) {
    const float length = glm::length(delta);
    if (length > 0.f) {
        return delta / length;
    }
    return glm::vec3{1.f, 0.f, 0.f};
}

[[nodiscard]] ContactManifold3D radial_hit(glm::vec3 center_a, float radius_a, glm::vec3 center_b, float radius_b) {
    const glm::vec3 delta = center_b - center_a;
    const float distance = glm::length(delta);
    const float radius_sum = radius_a + radius_b;
    if (distance > radius_sum) {
        return empty_manifold();
    }
    ContactManifold3D manifold;
    manifold.count = 1;
    manifold.normal = unit_or_x(delta);
    float penetration = radius_sum - distance;
    if (penetration < 0.f) {
        penetration = 0.f;
    }
    manifold.points[0].penetration = penetration;
    const glm::vec3 surface_a = center_a + manifold.normal * radius_a;
    const glm::vec3 surface_b = center_b - manifold.normal * radius_b;
    manifold.points[0].point = 0.5f * (surface_a + surface_b);
    return manifold;
}

[[nodiscard]] glm::vec3 capsule_axis(const ContactShape3D& capsule) {
    return glm::mat3_cast(capsule.rotation) * glm::vec3{0.f, 1.f, 0.f};
}

void capsule_segment(const ContactShape3D& capsule, glm::vec3& start, glm::vec3& end) {
    const glm::vec3 offset = capsule_axis(capsule) * (capsule.height * 0.5f);
    start = capsule.center - offset;
    end = capsule.center + offset;
}

[[nodiscard]] glm::vec3 closest_on_segment(glm::vec3 start, glm::vec3 end, glm::vec3 point) {
    const glm::vec3 edge = end - start;
    const float length2 = glm::dot(edge, edge);
    float t = 0.f;
    if (length2 > 0.f) {
        t = std::clamp(glm::dot(point - start, edge) / length2, 0.f, 1.f);
    }
    return start + edge * t;
}

// Parallel overlapping segments use the midpoint of the overlap. Otherwise Ericson's clamp.
void closest_segments(glm::vec3 p1, glm::vec3 q1, glm::vec3 p2, glm::vec3 q2, glm::vec3& c1, glm::vec3& c2) {
    const glm::vec3 d1 = q1 - p1;
    const glm::vec3 d2 = q2 - p2;
    const glm::vec3 r = p1 - p2;
    const float a = glm::dot(d1, d1);
    const float e = glm::dot(d2, d2);
    const float f = glm::dot(d2, r);
    constexpr float kDegenerate = 1.0e-8f;
    if (a <= kDegenerate && e <= kDegenerate) {
        c1 = p1;
        c2 = p2;
        return;
    }
    if (a <= kDegenerate) {
        c1 = p1;
        c2 = p2 + d2 * std::clamp(f / e, 0.f, 1.f);
        return;
    }
    const float c = glm::dot(d1, r);
    if (e <= kDegenerate) {
        c2 = p2;
        c1 = p1 + d1 * std::clamp(-c / a, 0.f, 1.f);
        return;
    }
    const float b = glm::dot(d1, d2);
    const float denom = a * e - b * b;
    if (std::abs(denom) <= kDegenerate * a * e) {
        const float length = std::sqrt(a);
        const glm::vec3 axis = d1 / length;
        const float b0 = glm::dot(p2 - p1, axis);
        const float b1 = glm::dot(q2 - p1, axis);
        const float overlap_lo = std::max(0.f, std::min(b0, b1));
        const float overlap_hi = std::min(length, std::max(b0, b1));
        const float mid = overlap_lo <= overlap_hi ? 0.5f * (overlap_lo + overlap_hi) : std::clamp(b0, 0.f, length);
        c1 = p1 + axis * mid;
        c2 = p2 + d2 * std::clamp(glm::dot(c1 - p2, d2) / e, 0.f, 1.f);
        return;
    }
    float s = std::clamp((b * f - c * e) / denom, 0.f, 1.f);
    float t = (b * s + f) / e;
    if (t < 0.f) {
        t = 0.f;
        s = std::clamp(-c / a, 0.f, 1.f);
    } else if (t > 1.f) {
        t = 1.f;
        s = std::clamp((b - c) / a, 0.f, 1.f);
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

[[nodiscard]] float axis_sign(float delta) {
    return delta < 0.f ? -1.f : 1.f;
}

[[nodiscard]] ContactManifold3D contact_sphere_box(const ContactShape3D& box, const ContactShape3D& sphere,
                                                   bool box_is_a) {
    const glm::mat3 basis = glm::mat3_cast(box.rotation);
    const glm::vec3 local = glm::transpose(basis) * (sphere.center - box.center);
    const glm::vec3 half = box.half_extents;
    const bool inside = std::abs(local.x) <= half.x && std::abs(local.y) <= half.y && std::abs(local.z) <= half.z;

    glm::vec3 normal_local{1.f, 0.f, 0.f};
    glm::vec3 box_local{0.f};
    float penetration = 0.f;
    if (!inside) {
        const glm::vec3 clamped = glm::clamp(local, -half, half);
        const glm::vec3 outside = local - clamped;
        const float dist2 = glm::dot(outside, outside);
        if (dist2 > sphere.radius * sphere.radius) {
            return empty_manifold();
        }
        const float dist = std::sqrt(dist2);
        if (dist > 0.f) {
            normal_local = outside / dist;
            penetration = sphere.radius - dist;
            if (penetration < 0.f) {
                penetration = 0.f;
            }
        } else {
            penetration = sphere.radius;
        }
        box_local = clamped;
    } else {
        const float face_x = half.x - std::abs(local.x);
        const float face_y = half.y - std::abs(local.y);
        const float face_z = half.z - std::abs(local.z);
        int axis = 0;
        float face = face_x;
        if (face_y < face) {
            axis = 1;
            face = face_y;
        }
        if (face_z < face) {
            axis = 2;
            face = face_z;
        }
        const float sign = axis_sign(local[axis]);
        normal_local = glm::vec3{0.f};
        normal_local[axis] = sign;
        penetration = face + sphere.radius;
        box_local = local;
        box_local[axis] = sign * half[axis];
    }

    const glm::vec3 normal_out = basis * normal_local;
    const glm::vec3 box_point = box.center + basis * box_local;
    const glm::vec3 sphere_point = sphere.center - normal_out * sphere.radius;
    ContactManifold3D manifold;
    manifold.count = 1;
    manifold.normal = box_is_a ? normal_out : -normal_out;
    manifold.points[0].penetration = penetration;
    manifold.points[0].point = 0.5f * (box_point + sphere_point);
    return manifold;
}

}

ContactManifold3D make_contact_3d(const ContactShape3D& a, const ContactShape3D& b) {
    if (!supported(a) || !supported(b)) {
        return empty_manifold();
    }
    const bool a_box = a.kind == ContactShapeKind3D::Box;
    const bool b_box = b.kind == ContactShapeKind3D::Box;
    const bool a_sphere = a.kind == ContactShapeKind3D::Sphere;
    const bool b_sphere = b.kind == ContactShapeKind3D::Sphere;
    const bool a_capsule = a.kind == ContactShapeKind3D::Capsule;
    const bool b_capsule = b.kind == ContactShapeKind3D::Capsule;

    ContactManifold3D manifold;
    if (a_sphere && b_sphere) {
        manifold = radial_hit(a.center, a.radius, b.center, b.radius);
    } else if (a_box && b_box) {
        manifold = contact_box_box_3d(a, b);
    } else if (a_box && b_sphere) {
        manifold = contact_sphere_box(a, b, true);
    } else if (a_sphere && b_box) {
        manifold = contact_sphere_box(b, a, false);
    } else if (a_sphere && b_capsule) {
        glm::vec3 start{0.f};
        glm::vec3 end{0.f};
        capsule_segment(b, start, end);
        manifold = radial_hit(a.center, a.radius, closest_on_segment(start, end, a.center), b.radius);
    } else if (a_capsule && b_sphere) {
        glm::vec3 start{0.f};
        glm::vec3 end{0.f};
        capsule_segment(a, start, end);
        manifold = radial_hit(closest_on_segment(start, end, b.center), a.radius, b.center, b.radius);
    } else if (a_capsule && b_capsule) {
        glm::vec3 a0{0.f};
        glm::vec3 a1{0.f};
        glm::vec3 b0{0.f};
        glm::vec3 b1{0.f};
        capsule_segment(a, a0, a1);
        capsule_segment(b, b0, b1);
        glm::vec3 closest_a{0.f};
        glm::vec3 closest_b{0.f};
        closest_segments(a0, a1, b0, b1, closest_a, closest_b);
        manifold = radial_hit(closest_a, a.radius, closest_b, b.radius);
    } else if (a_capsule && b_box) {
        manifold = contact_capsule_box_3d(a, b, true);
    } else if (a_box && b_capsule) {
        manifold = contact_capsule_box_3d(b, a, false);
    }
    return finite_or_fallback(manifold, a);
}

}
