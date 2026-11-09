#include "contact_2d.h"

#include <engine/ecs/sdf_2d.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>

// docs/tech/modules/ECS.md (SDF colliders). A body against one chunk of an SdfCollider2D field.

namespace engine {
namespace {

// Samples per polygon edge at most, so a huge polygon on a fine field stays bounded.
inline constexpr int kMaxEdgeSamples = 64;
inline constexpr int kMaxSamples = kMaxEdgeSamples * kContactPolygonCapacity;
// Cell indices past this read `outside` in the field (its 2^24-cell limit) and are owned by no chunk.
inline constexpr float kMaxCellIndex = 16777216.0f;

[[nodiscard]] bool finite_xy(glm::vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

// The field's frame: its entity's world position and yaw.
struct FieldFrame {
    glm::vec2 center{0.f};
    float cosine{1.f};
    float sine{0.f};

    [[nodiscard]] glm::vec2 to_local(glm::vec2 world) const {
        const glm::vec2 d = world - center;
        return {cosine * d.x + sine * d.y, -sine * d.x + cosine * d.y};
    }
    [[nodiscard]] glm::vec2 to_world_direction(glm::vec2 local) const {
        return {cosine * local.x - sine * local.y, sine * local.x + cosine * local.y};
    }
};

// A sample counts for the chunk that owns the cell it lies in, so a body over a seam is one contact, not two.
[[nodiscard]] bool owned_by(const SdfField2D& field, glm::ivec2 chunk, glm::vec2 local) {
    const glm::vec2 cell = glm::floor((local - field.origin()) / field.cell_size());
    if (!finite_xy(cell) || std::abs(cell.x) >= kMaxCellIndex || std::abs(cell.y) >= kMaxCellIndex) {
        return false;
    }
    return SdfField2D::chunk_of(glm::ivec2{static_cast<int>(cell.x), static_cast<int>(cell.y)}) == chunk;
}

[[nodiscard]] ContactManifold2D circle_against(const ContactShape2D& circle, const SdfField2D& field,
                                               const FieldFrame& frame, glm::ivec2 chunk) {
    ContactManifold2D manifold;
    const glm::vec2 local = frame.to_local(circle.center);
    if (!owned_by(field, chunk, local)) {
        return manifold;
    }
    const float distance = field.sample(local);
    if (!(distance <= circle.radius)) {
        return manifold;
    }
    const glm::vec2 gradient = field.gradient(local);
    if (gradient == glm::vec2{0.f}) {
        return manifold;
    }
    const glm::vec2 out = frame.to_world_direction(gradient);
    const glm::vec2 surface = circle.center - out * distance;
    const glm::vec2 deepest = circle.center - out * circle.radius;
    manifold.normal = -out;
    manifold.count = 1;
    manifold.points[0].penetration = circle.radius - distance;
    manifold.points[0].point = 0.5f * (surface + deepest);
    return manifold;
}

// World vertices of a box or polygon, CCW. False for a ring the polygon rules reject (contact_2d.h).
[[nodiscard]] bool body_ring(const ContactShape2D& body, std::array<glm::vec2, kContactPolygonCapacity>& ring,
                             int& count) {
    if (body.kind == ContactShapeKind::Aabb) {
        const glm::vec2 h = body.half_extents;
        const float c = std::cos(body.angle);
        const float s = std::sin(body.angle);
        const glm::vec2 local[4] = {{-h.x, -h.y}, {h.x, -h.y}, {h.x, h.y}, {-h.x, h.y}};
        for (int i = 0; i < 4; ++i) {
            ring[static_cast<std::size_t>(i)] =
                    body.center + glm::vec2{c * local[i].x - s * local[i].y, s * local[i].x + c * local[i].y};
        }
        count = 4;
        return true;
    }
    count = body.vertex_count;
    if (count < 3 || count > kContactPolygonCapacity) {
        return false;
    }
    float area = 0.f;
    for (int i = 0; i < count; ++i) {
        const glm::vec2 a = body.vertices[i];
        const glm::vec2 b = body.vertices[(i + 1) % count];
        const glm::vec2 c = body.vertices[(i + 2) % count];
        if (a == b) {
            return false;
        }
        const glm::vec2 e0 = b - a;
        const glm::vec2 e1 = c - b;
        if (e0.x * e1.y - e0.y * e1.x < 0.f) {
            return false;
        }
        area += a.x * b.y - b.x * a.y;
        ring[static_cast<std::size_t>(i)] = a;
    }
    return area > 0.f;
}

struct Penetration {
    glm::vec2 point{0.f};  // world, on the body
    glm::vec2 local{0.f};  // the same point in the field's frame
    float distance{0.f};   // <= 0
};

[[nodiscard]] ContactManifold2D ring_against(const ContactShape2D& body, const SdfField2D& field,
                                             const FieldFrame& frame, glm::ivec2 chunk) {
    ContactManifold2D manifold;
    std::array<glm::vec2, kContactPolygonCapacity> ring{};
    int count = 0;
    if (!body_ring(body, ring, count)) {
        return manifold;
    }
    // Vertices and points along each edge, at most one cell apart. Only penetrating samples are kept.
    std::array<Penetration, kMaxSamples> hits{};
    int hit_count = 0;
    int deepest = -1;
    const float cell = field.cell_size();
    for (int i = 0; i < count; ++i) {
        const glm::vec2 a = ring[static_cast<std::size_t>(i)];
        const glm::vec2 b = ring[static_cast<std::size_t>((i + 1) % count)];
        const float length = glm::length(b - a);
        const int steps = std::clamp(static_cast<int>(std::ceil(length / cell)), 1, kMaxEdgeSamples);
        for (int k = 0; k < steps; ++k) {
            const glm::vec2 point = a + (b - a) * (static_cast<float>(k) / static_cast<float>(steps));
            const glm::vec2 local = frame.to_local(point);
            if (!owned_by(field, chunk, local)) {
                continue;
            }
            const float distance = field.sample(local);
            if (!(distance <= 0.f)) {
                continue;
            }
            hits[static_cast<std::size_t>(hit_count)] = Penetration{point, local, distance};
            if (deepest < 0 || distance < hits[static_cast<std::size_t>(deepest)].distance) {
                deepest = hit_count;
            }
            ++hit_count;
        }
    }
    if (deepest < 0) {
        return manifold;
    }
    const Penetration& first = hits[static_cast<std::size_t>(deepest)];
    const glm::vec2 gradient = field.gradient(first.local);
    if (gradient == glm::vec2{0.f}) {
        return manifold;
    }
    const glm::vec2 out = frame.to_world_direction(gradient);
    manifold.normal = -out;
    manifold.count = 1;
    manifold.points[0].penetration = -first.distance;
    manifold.points[0].point = first.point - out * (first.distance * 0.5f);

    // The second point spans the contact: the penetrating sample farthest from the deepest along the surface.
    const glm::vec2 tangent{-out.y, out.x};
    int second = -1;
    float spread = 1.0e-3f * cell;
    for (int i = 0; i < hit_count; ++i) {
        const float along = std::abs(glm::dot(hits[static_cast<std::size_t>(i)].point - first.point, tangent));
        if (along > spread) {
            spread = along;
            second = i;
        }
    }
    if (second >= 0) {
        const Penetration& other = hits[static_cast<std::size_t>(second)];
        manifold.count = 2;
        manifold.points[1].penetration = -other.distance;
        manifold.points[1].point = other.point - out * (other.distance * 0.5f);
    }
    return manifold;
}

}

ContactManifold2D contact_sdf_2d(const ContactShape2D& body, const ContactShape2D& field) {
    if (field.sdf == nullptr || body.kind == ContactShapeKind::Sdf) {
        return {};
    }
    const FieldFrame frame{field.center, std::cos(field.angle), std::sin(field.angle)};
    if (body.kind == ContactShapeKind::Circle) {
        return circle_against(body, *field.sdf, frame, field.sdf_chunk);
    }
    return ring_against(body, *field.sdf, frame, field.sdf_chunk);
}

}
