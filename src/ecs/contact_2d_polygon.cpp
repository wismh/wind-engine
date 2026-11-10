#include "contact_2d.h"

#include <engine/core/det_math.h>
#include <engine/ecs/physics.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace engine {
namespace {

// Bias toward polygon A (matching Box2D's 0.1 * linearSlop) so near-equal SAT separations
// do not flip the reference face and normal direction across frames.
inline constexpr float kReferenceFaceBias = 0.1f * kContactSlop;
inline constexpr float kEqualPenetrationEps = 1.0e-6f;

struct Poly2D {
    glm::vec2 vertices[kContactPolygonCapacity]{};
    int count{0};
};

struct Face2D {
    float separation{0.f};
    int index{0};
    glm::vec2 normal{0.f};
    float offset{0.f};
};

[[nodiscard]] bool finite_xy(glm::vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] glm::vec2 rotate_z(glm::vec2 value, float angle) {
    const SinCos rotation = det_sin_cos(angle);
    const float c = rotation.cos;
    const float s = rotation.sin;
    return {c * value.x - s * value.y, s * value.x + c * value.y};
}

[[nodiscard]] bool outward_normal(glm::vec2 from, glm::vec2 to, glm::vec2& normal) {
    const glm::vec2 edge = to - from;
    const float length_sq = edge.x * edge.x + edge.y * edge.y;
    if (!(length_sq > 0.f) || !std::isfinite(length_sq)) {
        return false;
    }
    const float inv_length = 1.f / std::sqrt(length_sq);
    normal = glm::vec2{edge.y, -edge.x} * inv_length;
    return finite_xy(normal);
}

// CCW, no zero-length edge, positive area. A clockwise turn is rejected. A collinear turn is kept.
[[nodiscard]] bool convex_ccw(const Poly2D& poly) {
    if (poly.count < 3) {
        return false;
    }
    float area_sum = 0.f;
    for (int i = 0; i < poly.count; ++i) {
        const glm::vec2 v0 = poly.vertices[i];
        const glm::vec2 v1 = poly.vertices[(i + 1) % poly.count];
        const glm::vec2 v2 = poly.vertices[(i + 2) % poly.count];
        if (!finite_xy(v0) || !finite_xy(v1)) {
            return false;
        }
        const glm::vec2 edge0 = v1 - v0;
        const glm::vec2 edge1 = v2 - v1;
        if (edge0.x == 0.f && edge0.y == 0.f) {
            return false;
        }
        const float turn = edge0.x * edge1.y - edge0.y * edge1.x;
        if (turn < 0.f) {
            return false;
        }
        area_sum += v0.x * v1.y - v0.y * v1.x;
    }
    return area_sum > 0.f && std::isfinite(area_sum);
}

[[nodiscard]] bool build_polygon(const ContactShape2D& shape, Poly2D& poly) {
    poly = {};
    if (shape.kind == ContactShapeKind::Polygon) {
        if (shape.vertex_count < 3 || shape.vertex_count > kContactPolygonCapacity) {
            return false;
        }
        poly.count = shape.vertex_count;
        for (int i = 0; i < poly.count; ++i) {
            poly.vertices[i] = shape.vertices[i];
        }
        return convex_ccw(poly);
    }
    if (shape.kind != ContactShapeKind::Aabb || !finite_xy(shape.center) || !std::isfinite(shape.angle)) {
        return false;
    }
    if (!std::isfinite(shape.half_extents.x) || !std::isfinite(shape.half_extents.y) || shape.half_extents.x < 0.f ||
        shape.half_extents.y < 0.f) {
        return false;
    }
    const glm::vec2 local[4] = {
            {-shape.half_extents.x, -shape.half_extents.y},
            {shape.half_extents.x, -shape.half_extents.y},
            {shape.half_extents.x, shape.half_extents.y},
            {-shape.half_extents.x, shape.half_extents.y},
    };
    poly.count = 4;
    for (int i = 0; i < 4; ++i) {
        poly.vertices[i] = shape.center + rotate_z(local[i], shape.angle);
    }
    return convex_ccw(poly);
}

[[nodiscard]] bool best_face(const Poly2D& poly, const Poly2D& other, Face2D& face) {
    bool found = false;
    for (int i = 0; i < poly.count; ++i) {
        glm::vec2 normal{0.f};
        const glm::vec2 v0 = poly.vertices[i];
        if (!outward_normal(v0, poly.vertices[(i + 1) % poly.count], normal)) {
            return false;
        }
        float separation = glm::dot(normal, other.vertices[0] - v0);
        for (int j = 1; j < other.count; ++j) {
            separation = std::min(separation, glm::dot(normal, other.vertices[j] - v0));
        }
        if (!found || separation > face.separation) {
            found = true;
            face.separation = separation;
            face.index = i;
            face.normal = normal;
            face.offset = glm::dot(normal, v0);
        }
    }
    return found;
}

// Keeps points with dot(point, normal) <= offset. At most two.
int clip_segment(glm::vec2 p0, glm::vec2 p1, glm::vec2 normal, float offset, glm::vec2 out[2]) {
    const float d0 = glm::dot(p0, normal) - offset;
    const float d1 = glm::dot(p1, normal) - offset;
    int count = 0;
    if (d0 <= 0.f) {
        out[count++] = p0;
    }
    if (d1 <= 0.f && count < 2) {
        out[count++] = p1;
    }
    if (d0 * d1 < 0.f && count < 2) {
        const float t = d0 / (d0 - d1);
        out[count++] = p0 + (p1 - p0) * t;
    }
    return count;
}

[[nodiscard]] ContactManifold2D empty_manifold() {
    ContactManifold2D manifold;
    return manifold;
}

[[nodiscard]] ContactManifold2D contact_polygons(const Poly2D& a, const Poly2D& b) {
    Face2D face_a;
    Face2D face_b;
    if (!best_face(a, b, face_a) || !best_face(b, a, face_b)) {
        return empty_manifold();
    }
    if (face_a.separation > 0.f || face_b.separation > 0.f) {
        return empty_manifold();
    }

    const bool a_is_reference = face_a.separation + kReferenceFaceBias >= face_b.separation;
    const Face2D& reference = a_is_reference ? face_a : face_b;
    const Poly2D& ref_poly = a_is_reference ? a : b;
    const Poly2D& incident_poly = a_is_reference ? b : a;

    int incident = 0;
    bool have_incident = false;
    float incident_dot = 0.f;
    for (int i = 0; i < incident_poly.count; ++i) {
        glm::vec2 normal{0.f};
        if (!outward_normal(incident_poly.vertices[i], incident_poly.vertices[(i + 1) % incident_poly.count],
                            normal)) {
            return empty_manifold();
        }
        const float alignment = glm::dot(normal, reference.normal);
        if (!have_incident || alignment < incident_dot) {
            have_incident = true;
            incident_dot = alignment;
            incident = i;
        }
    }

    const glm::vec2 r0 = ref_poly.vertices[reference.index];
    const glm::vec2 r1 = ref_poly.vertices[(reference.index + 1) % ref_poly.count];
    const glm::vec2 edge = r1 - r0;
    const float edge_len_sq = edge.x * edge.x + edge.y * edge.y;
    if (!(edge_len_sq > 0.f)) {
        return empty_manifold();
    }
    const glm::vec2 side = edge * (1.f / std::sqrt(edge_len_sq));

    const glm::vec2 i0 = incident_poly.vertices[incident];
    const glm::vec2 i1 = incident_poly.vertices[(incident + 1) % incident_poly.count];
    glm::vec2 clipped[2];
    int clipped_count = clip_segment(i0, i1, -side, -glm::dot(side, r0), clipped);
    if (clipped_count == 2) {
        glm::vec2 next[2];
        clipped_count = clip_segment(clipped[0], clipped[1], side, glm::dot(side, r1), next);
        for (int i = 0; i < clipped_count; ++i) {
            clipped[i] = next[i];
        }
    } else if (clipped_count == 1) {
        glm::vec2 next[2];
        clipped_count = clip_segment(clipped[0], clipped[0], side, glm::dot(side, r1), next);
        for (int i = 0; i < clipped_count; ++i) {
            clipped[i] = next[i];
        }
    }
    if (clipped_count <= 0) {
        return empty_manifold();
    }

    ContactPoint2D kept[2];
    int kept_count = 0;
    for (int i = 0; i < clipped_count; ++i) {
        const float separation = glm::dot(reference.normal, clipped[i] - r0);
        if (separation <= 0.f && kept_count < 2) {
            kept[kept_count].point = clipped[i];
            kept[kept_count].penetration = -separation;
            ++kept_count;
        }
    }
    if (kept_count == 0) {
        return empty_manifold();
    }

    ContactManifold2D manifold;
    manifold.normal = a_is_reference ? reference.normal : -reference.normal;
    if (kept_count == 2 && std::abs(kept[0].penetration - kept[1].penetration) <= kEqualPenetrationEps) {
        manifold.count = 1;
        manifold.points[0].point = 0.5f * (kept[0].point + kept[1].point);
        manifold.points[0].penetration = 0.5f * (kept[0].penetration + kept[1].penetration);
        return manifold;
    }
    if (kept_count == 2 && kept[1].penetration > kept[0].penetration) {
        manifold.points[0] = kept[1];
        manifold.points[1] = kept[0];
    } else {
        manifold.points[0] = kept[0];
        if (kept_count == 2) {
            manifold.points[1] = kept[1];
        }
    }
    manifold.count = kept_count;
    return manifold;
}

[[nodiscard]] glm::vec2 closest_on_segment(glm::vec2 point, glm::vec2 from, glm::vec2 to) {
    const glm::vec2 edge = to - from;
    const float length_sq = edge.x * edge.x + edge.y * edge.y;
    if (!(length_sq > 0.f)) {
        return from;
    }
    const float t = std::clamp(glm::dot(point - from, edge) / length_sq, 0.f, 1.f);
    return from + edge * t;
}

[[nodiscard]] ContactManifold2D contact_polygon_circle(const Poly2D& poly, const ContactShape2D& circle,
                                                       bool polygon_is_a) {
    if (!std::isfinite(circle.radius) || circle.radius < 0.f || !finite_xy(circle.center)) {
        return empty_manifold();
    }

    bool inside = true;
    int inside_edge = 0;
    float inside_signed = 0.f;
    bool have_inside = false;
    glm::vec2 inside_normal{0.f};
    for (int i = 0; i < poly.count; ++i) {
        glm::vec2 normal{0.f};
        if (!outward_normal(poly.vertices[i], poly.vertices[(i + 1) % poly.count], normal)) {
            return empty_manifold();
        }
        const float signed_dist = glm::dot(normal, circle.center - poly.vertices[i]);
        if (signed_dist > 0.f) {
            inside = false;
        }
        if (!have_inside || signed_dist > inside_signed) {
            have_inside = true;
            inside_signed = signed_dist;
            inside_edge = i;
            inside_normal = normal;
        }
    }

    glm::vec2 normal_out{1.f, 0.f};
    glm::vec2 boundary{0.f};
    float penetration = 0.f;
    if (inside) {
        boundary = closest_on_segment(circle.center, poly.vertices[inside_edge],
                                      poly.vertices[(inside_edge + 1) % poly.count]);
        normal_out = inside_normal;
        penetration = circle.radius - inside_signed;
    } else {
        float best_dist_sq = 0.f;
        bool have = false;
        int best_edge = 0;
        for (int i = 0; i < poly.count; ++i) {
            const glm::vec2 candidate = closest_on_segment(circle.center, poly.vertices[i],
                                                           poly.vertices[(i + 1) % poly.count]);
            const glm::vec2 delta = circle.center - candidate;
            const float dist_sq = delta.x * delta.x + delta.y * delta.y;
            if (!have || dist_sq < best_dist_sq) {
                have = true;
                best_dist_sq = dist_sq;
                boundary = candidate;
                best_edge = i;
            }
        }
        if (best_dist_sq > circle.radius * circle.radius) {
            return empty_manifold();
        }
        const float dist = std::sqrt(best_dist_sq);
        if (dist > 0.f) {
            normal_out = (circle.center - boundary) * (1.f / dist);
            penetration = circle.radius - dist;
        } else if (!outward_normal(poly.vertices[best_edge], poly.vertices[(best_edge + 1) % poly.count],
                                   normal_out)) {
            return empty_manifold();
        } else {
            penetration = circle.radius;
        }
    }
    if (penetration < 0.f) {
        penetration = 0.f;
    }
    const glm::vec2 circle_point = circle.center - normal_out * circle.radius;
    ContactManifold2D manifold;
    manifold.count = 1;
    manifold.normal = polygon_is_a ? normal_out : -normal_out;
    manifold.points[0].penetration = penetration;
    manifold.points[0].point = 0.5f * (boundary + circle_point);
    return manifold;
}

}

ContactManifold2D contact_convex_2d(const ContactShape2D& a, const ContactShape2D& b) {
    const bool a_circle = a.kind == ContactShapeKind::Circle;
    const bool b_circle = b.kind == ContactShapeKind::Circle;
    if (a_circle && b_circle) {
        return empty_manifold();
    }
    if (a_circle || b_circle) {
        const ContactShape2D& circle = a_circle ? a : b;
        const ContactShape2D& other = a_circle ? b : a;
        Poly2D poly;
        if (!build_polygon(other, poly)) {
            return empty_manifold();
        }
        return contact_polygon_circle(poly, circle, !a_circle);
    }
    Poly2D poly_a;
    Poly2D poly_b;
    if (!build_polygon(a, poly_a) || !build_polygon(b, poly_b)) {
        return empty_manifold();
    }
    return contact_polygons(poly_a, poly_b);
}

}
