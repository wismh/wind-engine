#include <engine/ecs/ray.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine {
namespace {

constexpr float kParallel = 1e-6f;
constexpr float kDegenerate = 1e-8f;
constexpr float kEdge = 1e-5f;

glm::vec3 facing_ray(glm::vec3 normal, glm::vec3 direction) {
    if (glm::dot(normal, direction) > 0.0f) {
        return -normal;
    }
    return normal;
}

}

std::optional<RayHit> line_plane(const Ray& ray, glm::vec3 point, glm::vec3 normal) {
    const float normal_len = glm::length(normal);
    if (normal_len <= kDegenerate) {
        return std::nullopt;
    }
    const float denom = glm::dot(ray.direction, normal);
    if (std::abs(denom) <= kParallel * normal_len) {
        return std::nullopt;
    }
    RayHit hit;
    hit.t = glm::dot(point - ray.origin, normal) / denom;
    hit.point = ray.origin + hit.t * ray.direction;
    hit.normal = facing_ray(normal / normal_len, ray.direction);
    return hit;
}

std::optional<RayHit> ray_plane(const Ray& ray, glm::vec3 point, glm::vec3 normal) {
    const std::optional<RayHit> hit = line_plane(ray, point, normal);
    if (!hit || hit->t < 0.0f) {
        return std::nullopt;
    }
    return hit;
}

std::optional<RayHit> ray_sphere(const Ray& ray, glm::vec3 center, float radius) {
    if (radius < 0.0f) {
        return std::nullopt;
    }
    const float a = glm::dot(ray.direction, ray.direction);
    if (a <= kDegenerate) {
        return std::nullopt;
    }
    const glm::vec3 offset = ray.origin - center;
    const float b = glm::dot(offset, ray.direction);
    const float c = glm::dot(offset, offset) - radius * radius;
    const float discriminant = b * b - a * c;
    if (discriminant < 0.0f) {
        return std::nullopt;
    }
    const float root = std::sqrt(discriminant);
    const float t_near = (-b - root) / a;
    const float t_far = (-b + root) / a;
    RayHit hit;
    if (t_near >= 0.0f) {
        hit.t = t_near;
        hit.point = ray.origin + hit.t * ray.direction;
        const glm::vec3 out = hit.point - center;
        const float out_len = glm::length(out);
        hit.normal = out_len > kDegenerate ? out / out_len : facing_ray(glm::vec3{0.0f, 1.0f, 0.0f}, ray.direction);
    } else if (t_far >= 0.0f) {
        hit.t = 0.0f;
        hit.point = ray.origin;
        const float from_center = glm::length(offset);
        if (from_center > kDegenerate) {
            hit.normal = offset / from_center;
        } else {
            hit.normal = -glm::normalize(ray.direction);
        }
    } else {
        return std::nullopt;
    }
    hit.normal = facing_ray(hit.normal, ray.direction);
    return hit;
}

std::optional<RayHit> ray_aabb(const Ray& ray, glm::vec3 box_min, glm::vec3 box_max) {
    const float direction_len2 = glm::dot(ray.direction, ray.direction);
    if (direction_len2 <= kDegenerate) {
        return std::nullopt;
    }
    float t_enter = -std::numeric_limits<float>::infinity();
    float t_exit = std::numeric_limits<float>::infinity();
    glm::vec3 enter_normal{0.0f};
    for (int axis = 0; axis < 3; ++axis) {
        const float origin = ray.origin[axis];
        const float direction = ray.direction[axis];
        float lo = box_min[axis];
        float hi = box_max[axis];
        if (lo > hi) {
            std::swap(lo, hi);
        }
        if (std::abs(direction) <= kDegenerate) {
            if (origin < lo || origin > hi) {
                return std::nullopt;
            }
            continue;
        }
        float t0 = (lo - origin) / direction;
        float t1 = (hi - origin) / direction;
        float sign = -1.0f;
        if (t0 > t1) {
            std::swap(t0, t1);
            sign = 1.0f;
        }
        if (t0 > t_enter) {
            t_enter = t0;
            enter_normal = glm::vec3{0.0f};
            enter_normal[axis] = sign;
        }
        if (t1 < t_exit) {
            t_exit = t1;
        }
        if (t_enter > t_exit) {
            return std::nullopt;
        }
    }
    if (t_exit < 0.0f) {
        return std::nullopt;
    }
    RayHit hit;
    if (t_enter >= 0.0f) {
        hit.t = t_enter;
        hit.normal = enter_normal;
    } else {
        hit.t = 0.0f;
        hit.normal = -glm::normalize(ray.direction);
    }
    hit.point = ray.origin + hit.t * ray.direction;
    hit.normal = facing_ray(hit.normal, ray.direction);
    return hit;
}

std::optional<RayHit> ray_triangle(const Ray& ray, glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 p = glm::cross(ray.direction, ac);
    const float det = glm::dot(ab, p);
    if (std::abs(det) <= kDegenerate) {
        return std::nullopt;
    }
    const float inv = 1.0f / det;
    const glm::vec3 from_a = ray.origin - a;
    const float u = glm::dot(from_a, p) * inv;
    if (u < -kEdge || u > 1.0f + kEdge) {
        return std::nullopt;
    }
    const glm::vec3 q = glm::cross(from_a, ab);
    const float v = glm::dot(ray.direction, q) * inv;
    if (v < -kEdge || u + v > 1.0f + kEdge) {
        return std::nullopt;
    }
    const float t = glm::dot(ac, q) * inv;
    if (t < 0.0f) {
        return std::nullopt;
    }
    RayHit hit;
    hit.t = t;
    hit.point = ray.origin + t * ray.direction;
    const glm::vec3 n = glm::cross(ab, ac);
    const float n_len = glm::length(n);
    hit.normal = n_len > kDegenerate ? n / n_len : glm::vec3{0.0f, 0.0f, 1.0f};
    hit.normal = facing_ray(hit.normal, ray.direction);
    return hit;
}

std::optional<RayHit> ray_quad(const Ray& ray, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
    const std::optional<RayHit> abc = ray_triangle(ray, a, b, c);
    const std::optional<RayHit> acd = ray_triangle(ray, a, c, d);
    if (!abc) {
        return acd;
    }
    if (!acd) {
        return abc;
    }
    return abc->t <= acd->t ? abc : acd;
}

std::optional<RayHit> ray_segment(const Ray& ray, glm::vec3 start, glm::vec3 direction, float length, float radius) {
    if (length < 0.0f || radius < 0.0f) {
        return std::nullopt;
    }
    const float ray_len = glm::length(ray.direction);
    const float axis_len = glm::length(direction);
    if (ray_len <= kDegenerate || axis_len <= kDegenerate) {
        return std::nullopt;
    }
    const glm::vec3 r = ray.direction / ray_len;
    const glm::vec3 u = direction / axis_len;
    const glm::vec3 w = start - ray.origin;
    const float b = glm::dot(u, r);
    const float along_axis = glm::dot(u, w);
    const float along_ray = glm::dot(r, w);
    const float denom = 1.0f - b * b;
    float along = 0.0f;
    float t = 0.0f;
    if (denom > 1e-4f) {
        along = std::clamp((b * along_ray - along_axis) / denom, 0.0f, length);
        t = b * along + along_ray;
    } else {
        t = along_ray;
    }
    t = std::max(0.0f, t);
    const glm::vec3 on_segment = start + along * u;
    const glm::vec3 on_ray = ray.origin + t * r;
    const glm::vec3 gap = on_ray - on_segment;
    if (glm::dot(gap, gap) > radius * radius) {
        return std::nullopt;
    }
    RayHit hit;
    hit.t = t;
    hit.point = on_ray;
    const float gap_len = glm::length(gap);
    if (gap_len > kDegenerate) {
        hit.normal = gap / gap_len;
    } else if (std::abs(u.y) < 0.9f) {
        hit.normal = glm::normalize(glm::cross(u, glm::vec3{0.0f, 1.0f, 0.0f}));
    } else {
        hit.normal = glm::normalize(glm::cross(u, glm::vec3{1.0f, 0.0f, 0.0f}));
    }
    hit.normal = facing_ray(hit.normal, r);
    return hit;
}

}
