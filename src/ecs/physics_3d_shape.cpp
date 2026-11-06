#include "physics_3d_internal.h"

#include <glm/common.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace engine {
namespace {

[[nodiscard]] Aabb inactive_aabb() {
    return Aabb{glm::vec3{1.f}, glm::vec3{-1.f}};
}

[[nodiscard]] bool finite_vec(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool finite_quat(glm::quat rotation) {
    return std::isfinite(rotation.x) && std::isfinite(rotation.y) && std::isfinite(rotation.z) &&
           std::isfinite(rotation.w);
}

}

bool layers_match_3d(const ColliderItem3D& a, const ColliderItem3D& b) {
    return (a.layer & b.mask) != 0 && (b.layer & a.mask) != 0;
}

ContactShape3D contact_shape_of_3d(const ColliderItem3D& item) {
    ContactShape3D shape;
    shape.center = item.position;
    shape.rotation = item.rotation;
    if (item.shape == ColliderShape3D::Box) {
        shape.kind = ContactShapeKind3D::Box;
        shape.half_extents = item.box_size * 0.5f;
    } else if (item.shape == ColliderShape3D::Sphere) {
        shape.kind = ContactShapeKind3D::Sphere;
        shape.radius = item.radius;
    } else {
        shape.kind = ContactShapeKind3D::Capsule;
        shape.radius = item.radius;
        shape.height = item.height;
    }
    return shape;
}

Aabb aabb_of_3d(const ColliderItem3D& item) {
    if (!finite_vec(item.position) || !finite_quat(item.rotation)) {
        return inactive_aabb();
    }
    if (item.shape == ColliderShape3D::Sphere) {
        if (!std::isfinite(item.radius) || item.radius < 0.f) {
            return inactive_aabb();
        }
        const glm::vec3 half{item.radius};
        return Aabb{item.position - half, item.position + half};
    }
    if (item.shape == ColliderShape3D::Box) {
        if (!finite_vec(item.box_size) || item.box_size.x < 0.f || item.box_size.y < 0.f || item.box_size.z < 0.f) {
            return inactive_aabb();
        }
        const glm::mat3 rotation = glm::mat3_cast(item.rotation);
        const glm::vec3 half = item.box_size * 0.5f;
        glm::vec3 extent{0.f};
        const auto axis = [&](int row) {
            return std::abs(rotation[0][row]) * half.x + std::abs(rotation[1][row]) * half.y +
                   std::abs(rotation[2][row]) * half.z;
        };
        extent = {axis(0), axis(1), axis(2)};
        return Aabb{item.position - extent, item.position + extent};
    }
    if (!std::isfinite(item.radius) || !std::isfinite(item.height) || item.radius < 0.f || item.height < 0.f) {
        return inactive_aabb();
    }
    const glm::vec3 axis = glm::mat3_cast(item.rotation) * glm::vec3{0.f, 1.f, 0.f};
    const glm::vec3 offset = axis * (item.height * 0.5f);
    const glm::vec3 start = item.position - offset;
    const glm::vec3 end = item.position + offset;
    const glm::vec3 radius{item.radius};
    return Aabb{glm::min(start, end) - radius, glm::max(start, end) + radius};
}

}
