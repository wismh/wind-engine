#include "contact_2d.h"

#include <algorithm>
#include <cmath>

namespace engine {
namespace {

[[nodiscard]] bool finite_vec(glm::vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool supported(const ContactShape2D& shape) {
    if (!finite_vec(shape.center) || !std::isfinite(shape.angle)) {
        return false;
    }
    if (shape.kind == ContactShapeKind::Aabb) {
        return std::isfinite(shape.half_extents.x) && std::isfinite(shape.half_extents.y) &&
               shape.half_extents.x >= 0.f && shape.half_extents.y >= 0.f;
    }
    if (shape.kind == ContactShapeKind::Circle) {
        return std::isfinite(shape.radius) && shape.radius >= 0.f;
    }
    if (shape.kind == ContactShapeKind::Sdf) {
        return shape.sdf != nullptr;
    }
    if (shape.vertex_count < 3 || shape.vertex_count > kContactPolygonCapacity) {
        return false;
    }
    for (int i = 0; i < shape.vertex_count; ++i) {
        if (!finite_vec(shape.vertices[i])) {
            return false;
        }
    }
    return true;
}

// Zero delta takes the positive axis, so coincident centers resolve to +X when X wins the tie.
[[nodiscard]] float axis_sign(float delta) {
    return delta < 0.f ? -1.f : 1.f;
}

[[nodiscard]] ContactManifold2D empty_manifold() {
    ContactManifold2D manifold;
    return manifold;
}

[[nodiscard]] ContactManifold2D finite_or_fallback(const ContactManifold2D& manifold, const ContactShape2D& a) {
    if (manifold.count == 0) {
        return manifold;
    }
    bool ok = finite_vec(manifold.normal);
    for (int i = 0; i < manifold.count; ++i) {
        const ContactPoint2D& sample = manifold.points[i];
        ok = ok && std::isfinite(sample.penetration) && finite_vec(sample.point);
    }
    if (ok) {
        return manifold;
    }
    ContactManifold2D fallback;
    fallback.normal = glm::vec2{1.f, 0.f};
    fallback.count = 1;
    fallback.points[0].penetration = 0.f;
    fallback.points[0].point = finite_vec(a.center) ? a.center : glm::vec2{0.f};
    return fallback;
}

[[nodiscard]] ContactManifold2D contact_aabb_aabb(const ContactShape2D& a, const ContactShape2D& b) {
    const glm::vec2 delta = b.center - a.center;
    const float overlap_x = (a.half_extents.x + b.half_extents.x) - std::abs(delta.x);
    const float overlap_y = (a.half_extents.y + b.half_extents.y) - std::abs(delta.y);
    if (overlap_x < 0.f || overlap_y < 0.f) {
        return empty_manifold();
    }

    ContactManifold2D manifold;
    manifold.count = 1;
    if (overlap_x <= overlap_y) {
        manifold.normal = glm::vec2{axis_sign(delta.x), 0.f};
        manifold.points[0].penetration = overlap_x;
    } else {
        manifold.normal = glm::vec2{0.f, axis_sign(delta.y)};
        manifold.points[0].penetration = overlap_y;
    }

    const glm::vec2 a_min = a.center - a.half_extents;
    const glm::vec2 a_max = a.center + a.half_extents;
    const glm::vec2 b_min = b.center - b.half_extents;
    const glm::vec2 b_max = b.center + b.half_extents;
    const glm::vec2 overlap_min{std::max(a_min.x, b_min.x), std::max(a_min.y, b_min.y)};
    const glm::vec2 overlap_max{std::min(a_max.x, b_max.x), std::min(a_max.y, b_max.y)};
    manifold.points[0].point = 0.5f * (overlap_min + overlap_max);
    return manifold;
}

[[nodiscard]] ContactManifold2D contact_circle_circle(const ContactShape2D& a, const ContactShape2D& b) {
    const glm::vec2 delta = b.center - a.center;
    const float dist_sq = delta.x * delta.x + delta.y * delta.y;
    const float radius_sum = a.radius + b.radius;
    if (dist_sq > radius_sum * radius_sum) {
        return empty_manifold();
    }

    const float dist = std::sqrt(dist_sq);
    ContactManifold2D manifold;
    manifold.count = 1;
    if (dist > 0.f) {
        manifold.normal = delta / dist;
        const glm::vec2 a_surface = a.center + manifold.normal * a.radius;
        const glm::vec2 b_surface = b.center - manifold.normal * b.radius;
        manifold.points[0].point = 0.5f * (a_surface + b_surface);
    } else {
        manifold.normal = glm::vec2{1.f, 0.f};
        manifold.points[0].point = a.center;
    }
    float penetration = radius_sum - dist;
    if (penetration < 0.f) {
        penetration = 0.f;
    }
    manifold.points[0].penetration = penetration;
    return manifold;
}

struct BoxCircleHit {
    glm::vec2 n_out{1.f, 0.f};  // direction that moves the circle out of the box
    float penetration{0.f};
    glm::vec2 point{0.f};
    bool hit{false};
};

[[nodiscard]] BoxCircleHit contact_box_circle(const ContactShape2D& box, const ContactShape2D& circle) {
    const glm::vec2 box_min = box.center - box.half_extents;
    const glm::vec2 box_max = box.center + box.half_extents;
    const glm::vec2 clamped{
            std::clamp(circle.center.x, box_min.x, box_max.x),
            std::clamp(circle.center.y, box_min.y, box_max.y)};
    const glm::vec2 outside = circle.center - clamped;
    const float dist_sq = outside.x * outside.x + outside.y * outside.y;
    if (dist_sq > circle.radius * circle.radius) {
        return BoxCircleHit{};
    }

    BoxCircleHit hit;
    hit.hit = true;
    glm::vec2 box_point{0.f};
    glm::vec2 circle_point{0.f};
    if (dist_sq > 0.f) {
        const float dist = std::sqrt(dist_sq);
        hit.n_out = outside / dist;
        hit.penetration = circle.radius - dist;
        if (hit.penetration < 0.f) {
            hit.penetration = 0.f;
        }
        box_point = clamped;
        circle_point = circle.center - hit.n_out * circle.radius;
    } else {
        const float local_x = circle.center.x - box.center.x;
        const float local_y = circle.center.y - box.center.y;
        const float face_x = box.half_extents.x - std::abs(local_x);
        const float face_y = box.half_extents.y - std::abs(local_y);
        if (face_x <= face_y) {
            hit.n_out = glm::vec2{axis_sign(local_x), 0.f};
            hit.penetration = face_x + circle.radius;
            box_point = glm::vec2{circle.center.x + hit.n_out.x * face_x, circle.center.y};
        } else {
            hit.n_out = glm::vec2{0.f, axis_sign(local_y)};
            hit.penetration = face_y + circle.radius;
            box_point = glm::vec2{circle.center.x, circle.center.y + hit.n_out.y * face_y};
        }
        circle_point = circle.center - hit.n_out * circle.radius;
    }
    hit.point = 0.5f * (box_point + circle_point);
    return hit;
}

[[nodiscard]] ContactManifold2D from_box_circle(const BoxCircleHit& hit, bool box_is_a) {
    if (!hit.hit) {
        return empty_manifold();
    }
    ContactManifold2D manifold;
    manifold.count = 1;
    manifold.normal = box_is_a ? hit.n_out : -hit.n_out;
    manifold.points[0].penetration = hit.penetration;
    manifold.points[0].point = hit.point;
    return manifold;
}

}

ContactManifold2D make_contact_2d(const ContactShape2D& a, const ContactShape2D& b) {
    if (!supported(a) || !supported(b)) {
        return empty_manifold();
    }
    if (a.kind == ContactShapeKind::Sdf || b.kind == ContactShapeKind::Sdf) {
        if (a.kind == b.kind) {
            return empty_manifold();
        }
        if (b.kind == ContactShapeKind::Sdf) {
            return finite_or_fallback(contact_sdf_2d(a, b), a);
        }
        ContactManifold2D manifold = contact_sdf_2d(b, a);
        manifold.normal = -manifold.normal;
        return finite_or_fallback(manifold, a);
    }

    const bool a_aabb = a.kind == ContactShapeKind::Aabb && a.angle == 0.f;
    const bool b_aabb = b.kind == ContactShapeKind::Aabb && b.angle == 0.f;
    const bool a_circle = a.kind == ContactShapeKind::Circle;
    const bool b_circle = b.kind == ContactShapeKind::Circle;

    ContactManifold2D manifold;
    if (a_aabb && b_aabb) {
        manifold = contact_aabb_aabb(a, b);
    } else if (a_circle && b_circle) {
        manifold = contact_circle_circle(a, b);
    } else if (a_aabb && b_circle) {
        manifold = from_box_circle(contact_box_circle(a, b), true);
    } else if (a_circle && b_aabb) {
        manifold = from_box_circle(contact_box_circle(b, a), false);
    } else {
        manifold = contact_convex_2d(a, b);
    }
    return finite_or_fallback(manifold, a);
}

}
