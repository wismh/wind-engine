#include <engine/render/frustum.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace engine::render {

namespace {

// Relative slack before a box counts as outside, so a box that touches a plane is never lost to rounding.
constexpr float kFrustumTolerance = 1e-5f;

glm::vec4 row(const glm::mat4& m, int index) {
    return {m[0][index], m[1][index], m[2][index], m[3][index]};
}

bool finite(const glm::vec4& plane) {
    return std::isfinite(plane.x) && std::isfinite(plane.y) && std::isfinite(plane.z) && std::isfinite(plane.w);
}

// Unit normal when it has a length. A plane that is not finite becomes all zero, which every point is inside.
glm::vec4 normalized_plane(const glm::vec4& plane) {
    if (!finite(plane)) {
        return glm::vec4{0.0f};
    }
    const float length = glm::length(glm::vec3(plane));
    if (length > 0.0f && std::isfinite(length)) {
        return plane / length;
    }
    return plane;
}

}

Frustum make_frustum(const glm::mat4& view_projection) {
    const glm::vec4 x = row(view_projection, 0);
    const glm::vec4 y = row(view_projection, 1);
    const glm::vec4 z = row(view_projection, 2);
    const glm::vec4 w = row(view_projection, 3);
    Frustum frustum;
    frustum.planes = {
            normalized_plane(w + x), normalized_plane(w - x), normalized_plane(w + y),
            normalized_plane(w - y), normalized_plane(w + z), normalized_plane(w - z),
    };
    return frustum;
}

bool frustum_intersects(const Frustum& frustum, const Aabb& box) {
    for (const glm::vec4& plane : frustum.planes) {
        // The corner farthest along the normal: if even it is behind the plane, the whole box is.
        const glm::vec3 corner{
                plane.x >= 0.0f ? box.max.x : box.min.x,
                plane.y >= 0.0f ? box.max.y : box.min.y,
                plane.z >= 0.0f ? box.max.z : box.min.z,
        };
        const float along = glm::dot(glm::vec3(plane), corner);
        const float distance = along + plane.w;
        // NaN compares false, so a degenerate box or plane stays visible.
        if (distance < -kFrustumTolerance * (1.0f + std::abs(along) + std::abs(plane.w))) {
            return false;
        }
    }
    return true;
}

bool visible_in_any(std::span<const Frustum> frustums, const Aabb& box) {
    if (frustums.empty()) {
        return true;
    }
    return std::any_of(frustums.begin(), frustums.end(),
            [&](const Frustum& frustum) { return frustum_intersects(frustum, box); });
}

}
