#include "contact_3d.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine {
namespace {

struct Obb {
    glm::vec3 center{0.f};
    glm::vec3 axis[3]{};
    glm::vec3 half{0.f};
};

[[nodiscard]] Obb obb_from(const ContactShape3D& shape) {
    const glm::mat3 basis = glm::mat3_cast(shape.rotation);
    Obb box;
    box.center = shape.center;
    box.axis[0] = basis[0];
    box.axis[1] = basis[1];
    box.axis[2] = basis[2];
    box.half = shape.half_extents;
    return box;
}

[[nodiscard]] bool exact_identity(glm::quat rotation) {
    return rotation.w == 1.f && rotation.x == 0.f && rotation.y == 0.f && rotation.z == 0.f;
}

[[nodiscard]] float axis_sign(float delta) {
    return delta < 0.f ? -1.f : 1.f;
}

[[nodiscard]] ContactManifold3D contact_aabb(const ContactShape3D& a, const ContactShape3D& b) {
    const glm::vec3 delta = b.center - a.center;
    const float overlap_x = (a.half_extents.x + b.half_extents.x) - std::abs(delta.x);
    const float overlap_y = (a.half_extents.y + b.half_extents.y) - std::abs(delta.y);
    const float overlap_z = (a.half_extents.z + b.half_extents.z) - std::abs(delta.z);
    if (overlap_x < 0.f || overlap_y < 0.f || overlap_z < 0.f) {
        return ContactManifold3D{};
    }

    ContactManifold3D manifold;
    manifold.count = 1;
    if (overlap_x <= overlap_y && overlap_x <= overlap_z) {
        manifold.normal = glm::vec3{axis_sign(delta.x), 0.f, 0.f};
        manifold.points[0].penetration = overlap_x;
    } else if (overlap_y <= overlap_z) {
        manifold.normal = glm::vec3{0.f, axis_sign(delta.y), 0.f};
        manifold.points[0].penetration = overlap_y;
    } else {
        manifold.normal = glm::vec3{0.f, 0.f, axis_sign(delta.z)};
        manifold.points[0].penetration = overlap_z;
    }

    const glm::vec3 a_min = a.center - a.half_extents;
    const glm::vec3 a_max = a.center + a.half_extents;
    const glm::vec3 b_min = b.center - b.half_extents;
    const glm::vec3 b_max = b.center + b.half_extents;
    const glm::vec3 overlap_min{std::max(a_min.x, b_min.x), std::max(a_min.y, b_min.y), std::max(a_min.z, b_min.z)};
    const glm::vec3 overlap_max{std::min(a_max.x, b_max.x), std::min(a_max.y, b_max.y), std::min(a_max.z, b_max.z)};
    manifold.points[0].point = 0.5f * (overlap_min + overlap_max);
    return manifold;
}

[[nodiscard]] float projected_radius(const Obb& box, glm::vec3 axis) {
    return box.half.x * std::abs(glm::dot(axis, box.axis[0])) + box.half.y * std::abs(glm::dot(axis, box.axis[1])) +
           box.half.z * std::abs(glm::dot(axis, box.axis[2]));
}

[[nodiscard]] glm::vec3 support_point(const Obb& box, glm::vec3 direction) {
    glm::vec3 local_sign{1.f};
    for (int axis = 0; axis < 3; ++axis) {
        if (glm::dot(direction, box.axis[axis]) < 0.f) {
            local_sign[axis] = -1.f;
        }
    }
    return box.center + box.axis[0] * (local_sign.x * box.half.x) + box.axis[1] * (local_sign.y * box.half.y) +
           box.axis[2] * (local_sign.z * box.half.z);
}

[[nodiscard]] glm::vec3 edge_origin(const Obb& box, int edge_axis, glm::vec3 toward) {
    glm::vec3 origin = box.center;
    for (int axis = 0; axis < 3; ++axis) {
        if (axis == edge_axis) {
            continue;
        }
        const float sign = glm::dot(toward, box.axis[axis]) < 0.f ? -1.f : 1.f;
        origin += box.axis[axis] * (sign * box.half[axis]);
    }
    return origin;
}

void closest_segments(glm::vec3 p1, glm::vec3 q1, glm::vec3 p2, glm::vec3 q2, glm::vec3& c1, glm::vec3& c2) {
    const glm::vec3 d1 = q1 - p1;
    const glm::vec3 d2 = q2 - p2;
    const float a = glm::dot(d1, d1);
    const float e = glm::dot(d2, d2);
    const float f = glm::dot(d2, p1 - p2);
    const float c = glm::dot(d1, p1 - p2);
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
    if (e <= kDegenerate) {
        c2 = p2;
        c1 = p1 + d1 * std::clamp(-c / a, 0.f, 1.f);
        return;
    }
    const float b = glm::dot(d1, d2);
    const float denom = a * e - b * b;
    float s = 0.f;
    float t = 0.f;
    if (std::abs(denom) > kDegenerate) {
        s = std::clamp((b * f - c * e) / denom, 0.f, 1.f);
    }
    t = (b * s + f) / e;
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

[[nodiscard]] ContactManifold3D contact_sat(const ContactShape3D& shape_a, const ContactShape3D& shape_b) {
    const Obb a = obb_from(shape_a);
    const Obb b = obb_from(shape_b);
    float best_pen = std::numeric_limits<float>::infinity();
    glm::vec3 best_axis{1.f, 0.f, 0.f};
    int best_kind = 0;
    int best_i = 0;
    int best_j = 0;
    bool separated = false;

    auto consider = [&](glm::vec3 axis, int kind, int ia, int ib) {
        if (separated) {
            return;
        }
        const float length2 = glm::dot(axis, axis);
        if (length2 < 1.0e-8f) {
            return;
        }
        axis /= std::sqrt(length2);
        float distance = glm::dot(b.center - a.center, axis);
        if (distance < 0.f) {
            axis = -axis;
            distance = -distance;
        }
        const float penetration = projected_radius(a, axis) + projected_radius(b, axis) - distance;
        if (penetration < 0.f) {
            separated = true;
            return;
        }
        if (penetration < best_pen) {
            best_pen = penetration;
            best_axis = axis;
            best_kind = kind;
            best_i = ia;
            best_j = ib;
        }
    };

    for (int axis = 0; axis < 3; ++axis) {
        consider(a.axis[axis], 0, axis, 0);
        consider(b.axis[axis], 1, 0, axis);
    }
    for (int i = 0; i < 3 && !separated; ++i) {
        for (int j = 0; j < 3; ++j) {
            consider(glm::cross(a.axis[i], b.axis[j]), 2, i, j);
        }
    }
    if (separated || !std::isfinite(best_pen)) {
        return ContactManifold3D{};
    }

    glm::vec3 point{0.f};
    if (best_kind == 0) {
        const glm::vec3 face_point = a.center + best_axis * a.half[best_i];
        const glm::vec3 incident = support_point(b, -best_axis);
        point = incident - best_axis * glm::dot(incident - face_point, best_axis);
    } else if (best_kind == 1) {
        const glm::vec3 face_point = b.center - best_axis * b.half[best_j];
        const glm::vec3 incident = support_point(a, best_axis);
        point = incident - best_axis * glm::dot(incident - face_point, best_axis);
    } else {
        const glm::vec3 origin_a = edge_origin(a, best_i, best_axis);
        const glm::vec3 origin_b = edge_origin(b, best_j, -best_axis);
        glm::vec3 closest_a{0.f};
        glm::vec3 closest_b{0.f};
        closest_segments(origin_a - a.axis[best_i] * a.half[best_i], origin_a + a.axis[best_i] * a.half[best_i],
                         origin_b - b.axis[best_j] * b.half[best_j], origin_b + b.axis[best_j] * b.half[best_j],
                         closest_a, closest_b);
        point = 0.5f * (closest_a + closest_b);
    }

    ContactManifold3D manifold;
    manifold.count = 1;
    manifold.normal = best_axis;
    manifold.points[0].penetration = best_pen;
    manifold.points[0].point = point;
    return manifold;
}

}

ContactManifold3D contact_box_box_3d(const ContactShape3D& a, const ContactShape3D& b) {
    if (exact_identity(a.rotation) && exact_identity(b.rotation)) {
        return contact_aabb(a, b);
    }
    return contact_sat(a, b);
}

}
