#include "physics_2d_internal.h"

#include <algorithm>
#include <cmath>

namespace engine {
namespace {

[[nodiscard]] Aabb inactive_aabb() {
    return Aabb{glm::vec3{1.f}, glm::vec3{-1.f}};
}

[[nodiscard]] bool finite_xy(glm::vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

void rotate_polygon(const ColliderItem2D& item, ContactShape2D& shape) {
    const float cosine = std::cos(item.angle);
    const float sine = std::sin(item.angle);
    const glm::vec2 center{item.position.x, item.position.y};
    const int count = std::min(item.polygon_count, kPolygonCollider2DCapacity);
    shape.vertex_count = count;
    for (int i = 0; i < count; ++i) {
        const glm::vec2 local = item.polygon[i];
        shape.vertices[i] = center + glm::vec2{cosine * local.x - sine * local.y, sine * local.x + cosine * local.y};
    }
}

[[nodiscard]] Aabb aabb_from_xy(glm::vec2 min_xy, glm::vec2 max_xy) {
    Aabb box;
    box.min = {min_xy.x, min_xy.y, -1.f};
    box.max = {max_xy.x, max_xy.y, 1.f};
    return box;
}

}

CompoundShape2D box_shape_2d(glm::vec2 center, glm::vec2 size, float angle) {
    const glm::vec2 half = size * 0.5f;
    const glm::vec2 corners[4] = {{-half.x, -half.y}, {half.x, -half.y}, {half.x, half.y}, {-half.x, half.y}};
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    CompoundShape2D shape;
    shape.count = 4;
    for (int i = 0; i < 4; ++i) {
        const glm::vec2 corner = corners[i];
        const glm::vec2 rotated{cosine * corner.x - sine * corner.y, sine * corner.x + cosine * corner.y};
        shape.vertices[i] = center + rotated;
    }
    return shape;
}

// ColliderShape2D::Compound is a polygon in every function below: the final else branch.
bool layers_match_2d(const ColliderItem2D& a, const ColliderItem2D& b) {
    return (a.layer & b.mask) != 0 && (b.layer & a.mask) != 0;
}

ContactShape2D contact_shape_of_2d(const ColliderItem2D& item) {
    ContactShape2D shape;
    shape.center = glm::vec2{item.position.x, item.position.y};
    shape.angle = item.angle;
    if (item.shape == ColliderShape2D::Box) {
        shape.kind = ContactShapeKind::Aabb;
        shape.half_extents = glm::vec2{item.box_size.x * 0.5f, item.box_size.y * 0.5f};
    } else if (item.shape == ColliderShape2D::Circle) {
        shape.kind = ContactShapeKind::Circle;
        shape.radius = item.radius;
    } else {
        shape.kind = ContactShapeKind::Polygon;
        rotate_polygon(item, shape);
    }
    return shape;
}

Aabb aabb_of_2d(const ColliderItem2D& item) {
    const glm::vec2 center{item.position.x, item.position.y};
    if (!finite_xy(center)) {
        return inactive_aabb();
    }
    if (item.shape == ColliderShape2D::Box) {
        if (!std::isfinite(item.box_size.x) || !std::isfinite(item.box_size.y) || item.box_size.x < 0.f ||
            item.box_size.y < 0.f || !std::isfinite(item.angle)) {
            return inactive_aabb();
        }
        const glm::vec2 half{item.box_size.x * 0.5f, item.box_size.y * 0.5f};
        if (item.angle == 0.f) {
            return aabb_from_xy(center - half, center + half);
        }
        const float cosine = std::cos(item.angle);
        const float sine = std::sin(item.angle);
        const glm::vec2 local[4] = {{-half.x, -half.y}, {half.x, -half.y}, {half.x, half.y}, {-half.x, half.y}};
        glm::vec2 lo = center;
        glm::vec2 hi = center;
        for (const glm::vec2& corner : local) {
            const glm::vec2 rotated{cosine * corner.x - sine * corner.y, sine * corner.x + cosine * corner.y};
            const glm::vec2 world = center + rotated;
            lo.x = std::min(lo.x, world.x);
            lo.y = std::min(lo.y, world.y);
            hi.x = std::max(hi.x, world.x);
            hi.y = std::max(hi.y, world.y);
        }
        return aabb_from_xy(lo, hi);
    }
    if (item.shape == ColliderShape2D::Circle) {
        if (!std::isfinite(item.radius) || item.radius < 0.f) {
            return inactive_aabb();
        }
        const glm::vec2 half{item.radius, item.radius};
        return aabb_from_xy(center - half, center + half);
    }
    if (item.polygon_count < 3 || !std::isfinite(item.angle)) {
        return inactive_aabb();
    }
    const ContactShape2D shape = contact_shape_of_2d(item);
    glm::vec2 lo = shape.vertices[0];
    glm::vec2 hi = shape.vertices[0];
    if (!finite_xy(lo)) {
        return inactive_aabb();
    }
    for (int i = 1; i < shape.vertex_count; ++i) {
        const glm::vec2 vertex = shape.vertices[i];
        if (!finite_xy(vertex)) {
            return inactive_aabb();
        }
        lo.x = std::min(lo.x, vertex.x);
        lo.y = std::min(lo.y, vertex.y);
        hi.x = std::max(hi.x, vertex.x);
        hi.y = std::max(hi.y, vertex.y);
    }
    return aabb_from_xy(lo, hi);
}

}
