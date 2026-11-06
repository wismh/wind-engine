#include "contact_3d.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine {
namespace {

[[nodiscard]] float axis_sign(float delta) {
    return delta < 0.f ? -1.f : 1.f;
}

[[nodiscard]] glm::vec3 on_segment(glm::vec3 start, glm::vec3 delta, float t) {
    return start + delta * t;
}

// Solid AABB. Overlap reports the deepest interior point. Separation reports the closest boundary pair.
struct SegmentBoxQuery {
    bool overlaps{false};
    float distance{0.f};
    float depth{0.f};
    glm::vec3 segment_point{0.f};
    glm::vec3 box_point{0.f};
    glm::vec3 normal_local{1.f, 0.f, 0.f};  // from the box toward the segment
};

[[nodiscard]] SegmentBoxQuery query_segment_aabb(glm::vec3 start, glm::vec3 end, glm::vec3 half) {
    const glm::vec3 delta = end - start;
    float splits[8];
    int split_count = 0;
    splits[split_count++] = 0.f;
    splits[split_count++] = 1.f;
    for (int axis = 0; axis < 3; ++axis) {
        if (delta[axis] == 0.f) {
            continue;
        }
        const float faces[2] = {-half[axis], half[axis]};
        for (float face : faces) {
            const float t = (face - start[axis]) / delta[axis];
            if (t > 0.f && t < 1.f) {
                splits[split_count++] = t;
            }
        }
    }
    std::sort(splits, splits + split_count);
    int unique = 1;
    for (int i = 1; i < split_count; ++i) {
        if (splits[i] != splits[unique - 1]) {
            splits[unique++] = splits[i];
        }
    }

    bool overlaps = false;
    float inside_lo = 1.f;
    float inside_hi = 0.f;
    float best_d2 = std::numeric_limits<float>::infinity();
    float best_t = 0.f;

    for (int i = 0; i < unique - 1; ++i) {
        const float t0 = splits[i];
        const float t1 = splits[i + 1];
        if (t1 <= t0) {
            continue;
        }
        const float mid = 0.5f * (t0 + t1);
        const glm::vec3 sample = on_segment(start, delta, mid);
        float quad_a = 0.f;
        float quad_b = 0.f;
        float quad_c = 0.f;
        bool outside = false;
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(sample[axis]) <= half[axis]) {
                continue;
            }
            outside = true;
            const float bound = sample[axis] < 0.f ? -half[axis] : half[axis];
            const float excess0 = start[axis] - bound;
            quad_a += delta[axis] * delta[axis];
            quad_b += 2.f * delta[axis] * excess0;
            quad_c += excess0 * excess0;
        }
        if (!outside) {
            overlaps = true;
            inside_lo = std::min(inside_lo, t0);
            inside_hi = std::max(inside_hi, t1);
            continue;
        }
        float t_star = t0;
        if (quad_a > 0.f) {
            t_star = std::clamp(-quad_b / (2.f * quad_a), t0, t1);
        } else if (quad_b * t1 < quad_b * t0) {
            t_star = t1;
        }
        const float dist2 = quad_a * t_star * t_star + quad_b * t_star + quad_c;
        if (dist2 < best_d2) {
            best_d2 = dist2;
            best_t = t_star;
        }
    }

    SegmentBoxQuery query;
    if (overlaps) {
        float zeros[5];
        int zero_count = 0;
        zeros[zero_count++] = inside_lo;
        zeros[zero_count++] = inside_hi;
        for (int axis = 0; axis < 3; ++axis) {
            if (delta[axis] == 0.f) {
                continue;
            }
            const float t = (0.f - start[axis]) / delta[axis];
            if (t > inside_lo && t < inside_hi) {
                zeros[zero_count++] = t;
            }
        }
        std::sort(zeros, zeros + zero_count);

        float best_depth = -1.f;
        float deepest_t = inside_lo;
        for (int i = 0; i < zero_count - 1; ++i) {
            const float t0 = zeros[i];
            const float t1 = zeros[i + 1];
            if (t1 < t0) {
                continue;
            }
            const glm::vec3 mid = on_segment(start, delta, 0.5f * (t0 + t1));
            float sign[3];
            for (int axis = 0; axis < 3; ++axis) {
                sign[axis] = axis_sign(mid[axis]);
            }
            auto depth_of = [&](float t) {
                const glm::vec3 point = on_segment(start, delta, t);
                float depth = half[0] - sign[0] * point[0];
                for (int axis = 1; axis < 3; ++axis) {
                    depth = std::min(depth, half[axis] - sign[axis] * point[axis]);
                }
                return depth;
            };
            auto consider = [&](float t) {
                if (t < t0 || t > t1) {
                    return;
                }
                const float depth = depth_of(t);
                if (depth > best_depth) {
                    best_depth = depth;
                    deepest_t = t;
                }
            };
            consider(t0);
            consider(t1);
            for (int i_axis = 0; i_axis < 3; ++i_axis) {
                for (int j_axis = i_axis + 1; j_axis < 3; ++j_axis) {
                    const float denom = sign[i_axis] * delta[i_axis] - sign[j_axis] * delta[j_axis];
                    if (denom == 0.f) {
                        continue;
                    }
                    const float left = half[i_axis] - sign[i_axis] * start[i_axis];
                    const float right = half[j_axis] - sign[j_axis] * start[j_axis];
                    consider((left - right) / denom);
                }
            }
        }

        const glm::vec3 point = on_segment(start, delta, deepest_t);
        int axis = 0;
        float face = half[0] - std::abs(point[0]);
        for (int next = 1; next < 3; ++next) {
            const float candidate = half[next] - std::abs(point[next]);
            if (candidate < face) {
                face = candidate;
                axis = next;
            }
        }
        const float sign = axis_sign(point[axis]);
        query.overlaps = true;
        query.depth = std::max(0.f, face);
        query.segment_point = point;
        query.box_point = point;
        query.box_point[axis] = sign * half[axis];
        query.normal_local = glm::vec3{0.f};
        query.normal_local[axis] = sign;
        return query;
    }

    const glm::vec3 segment_point = on_segment(start, delta, best_t);
    const glm::vec3 box_point = glm::clamp(segment_point, -half, half);
    query.distance = glm::length(segment_point - box_point);
    query.segment_point = segment_point;
    query.box_point = box_point;
    query.normal_local = query.distance > 0.f ? (segment_point - box_point) / query.distance : glm::vec3{1.f, 0.f, 0.f};
    return query;
}

}

ContactManifold3D contact_capsule_box_3d(const ContactShape3D& capsule, const ContactShape3D& box, bool capsule_is_a) {
    const glm::mat3 basis = glm::mat3_cast(box.rotation);
    const glm::vec3 axis = glm::mat3_cast(capsule.rotation) * glm::vec3{0.f, 1.f, 0.f};
    const glm::vec3 offset = axis * (capsule.height * 0.5f);
    const glm::mat3 to_local = glm::transpose(basis);
    const glm::vec3 start = to_local * (capsule.center - offset - box.center);
    const glm::vec3 end = to_local * (capsule.center + offset - box.center);
    const SegmentBoxQuery query = query_segment_aabb(start, end, box.half_extents);

    float penetration = 0.f;
    if (query.overlaps) {
        penetration = capsule.radius + query.depth;
    } else if (query.distance <= capsule.radius) {
        penetration = capsule.radius - query.distance;
        if (penetration < 0.f) {
            penetration = 0.f;
        }
    } else {
        return ContactManifold3D{};
    }

    const glm::vec3 normal_out = basis * query.normal_local;
    const glm::vec3 box_point = box.center + basis * query.box_point;
    const glm::vec3 segment_point = box.center + basis * query.segment_point;
    const glm::vec3 capsule_surface = segment_point - normal_out * capsule.radius;
    ContactManifold3D manifold;
    manifold.count = 1;
    manifold.normal = capsule_is_a ? -normal_out : normal_out;
    manifold.points[0].penetration = penetration;
    manifold.points[0].point = 0.5f * (box_point + capsule_surface);
    return manifold;
}

}
