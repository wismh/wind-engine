#pragma once

// docs/tech/modules/Render.md (Frustum culling)

#include <engine/render/aabb.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <span>

namespace engine::render {

// The six planes of a clip volume: left, right, bottom, top, near, far. `xyz` is the unit normal pointing inside and
// `w` the offset, so a point `p` is inside a plane when `dot(xyz, p) + w >= 0`.
struct Frustum {
    std::array<glm::vec4, 6> planes{};
};

// The clip volume -w <= x, y, z <= w of `view_projection`, in the space that matrix maps from: world space for
// `projection * view`, local space for `projection * view * model`. Orthographic and perspective alike.
[[nodiscard]] Frustum make_frustum(const glm::mat4& view_projection);

// False only when the whole box lies outside one plane. A box that touches a plane is inside, and so is any box
// tested against a plane that is not finite (a degenerate matrix never culls). Conservative: a box just outside a
// corner of the volume can still be reported inside.
[[nodiscard]] bool frustum_intersects(const Frustum& frustum, const Aabb& box);

// True when `frustums` is empty (no culling) or at least one of them intersects `box`.
[[nodiscard]] bool visible_in_any(std::span<const Frustum> frustums, const Aabb& box);

}
