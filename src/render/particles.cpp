#include <engine/render/particles.h>

#include "render/particle_shape.h"

#include <engine/core/job_system.h>
#include <engine/ecs/grid_query.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <span>
#include <vector>

namespace engine::render {
namespace {

thread_local std::mt19937 g_rng{std::random_device{}()};

float rand_range(float min, float max) {
    if (min >= max) {
        return min;
    }
    std::uniform_real_distribution<float> dist(min, max);
    return dist(g_rng);
}

glm::vec2 rand_vec2(const glm::vec2& min, const glm::vec2& max) {
    return {rand_range(min.x, max.x), rand_range(min.y, max.y)};
}

glm::vec3 random_in_unit_sphere() {
    for (int i = 0; i < 16; ++i) {
        glm::vec3 p{rand_range(-1.0f, 1.0f), rand_range(-1.0f, 1.0f), rand_range(-1.0f, 1.0f)};
        if (glm::dot(p, p) <= 1.0f && glm::dot(p, p) > 1e-6f) {
            return glm::normalize(p);
        }
    }
    return {0.0f, 1.0f, 0.0f};
}

glm::vec3 compute_shape_offset(EmitterShape shape, const glm::vec3& scale, const glm::vec3& direction) {
    switch (shape) {
        case EmitterShape::Point:
            return {0.0f, 0.0f, 0.0f};
        case EmitterShape::Box:
            return {
                    rand_range(-0.5f, 0.5f) * scale.x,
                    rand_range(-0.5f, 0.5f) * scale.y,
                    rand_range(-0.5f, 0.5f) * scale.z,
            };
        case EmitterShape::Circle: {
            const float angle = rand_range(0.0f, glm::two_pi<float>());
            const float r = std::sqrt(rand_range(0.0f, 1.0f));
            return {std::cos(angle) * r * scale.x, std::sin(angle) * r * scale.y, 0.0f};
        }
        case EmitterShape::Cone:
            return {0.0f, 0.0f, 0.0f};
        case EmitterShape::Sphere: {
            const float u = rand_range(0.0f, 1.0f);
            const float v = rand_range(0.0f, 1.0f);
            const float w = rand_range(0.0f, 1.0f);
            return particle_shape_offset(shape, scale, direction, u, v, w);
        }
        case EmitterShape::Disc: {
            // Angle first, then the radius source, the same order Circle draws.
            const float v = rand_range(0.0f, 1.0f);
            const float u = rand_range(0.0f, 1.0f);
            return particle_shape_offset(shape, scale, direction, u, v, 0.0f);
        }
    }
    return {0.0f, 0.0f, 0.0f};
}

glm::vec3 compute_direction(const glm::vec3& base_dir, float spread_angle) {
    glm::vec3 dir = base_dir;
    if (glm::dot(dir, dir) < 1e-6f) {
        dir = {0.0f, 1.0f, 0.0f};
    } else {
        dir = glm::normalize(dir);
    }

    if (spread_angle <= 0.0f) {
        return dir;
    }

    const glm::vec3 random_vec = random_in_unit_sphere();
    const float factor = std::tan(rand_range(0.0f, spread_angle));
    glm::vec3 perturbed = dir + random_vec * factor;
    if (glm::dot(perturbed, perturbed) > 1e-6f) {
        return glm::normalize(perturbed);
    }
    return dir;
}

struct HitResult {
    bool hit = false;
    float t = 1.0f;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f};
};

bool check_box_collision(
        const glm::vec3& p0,
        const glm::vec3& p1,
        float radius,
        const glm::vec3& box_pos,
        const glm::vec3& box_size,
        HitResult& hit) {
    const glm::vec3 half = box_size * 0.5f;
    const float min_x = box_pos.x - half.x - radius;
    const float max_x = box_pos.x + half.x + radius;
    const float min_y = box_pos.y - half.y - radius;
    const float max_y = box_pos.y + half.y + radius;

    // Check if p0 is already inside
    if (p0.x >= min_x && p0.x <= max_x && p0.y >= min_y && p0.y <= max_y) {
        const float d_left = p0.x - min_x;
        const float d_right = max_x - p0.x;
        const float d_bottom = p0.y - min_y;
        const float d_top = max_y - p0.y;

        float min_d = d_left;
        glm::vec3 norm{-1.0f, 0.0f, 0.0f};
        if (d_right < min_d) {
            min_d = d_right;
            norm = glm::vec3{1.0f, 0.0f, 0.0f};
        }
        if (d_bottom < min_d) {
            min_d = d_bottom;
            norm = glm::vec3{0.0f, -1.0f, 0.0f};
        }
        if (d_top < min_d) {
            norm = glm::vec3{0.0f, 1.0f, 0.0f};
        }

        hit.hit = true;
        hit.t = 0.0f;
        hit.point = p0;
        hit.normal = norm;
        return true;
    }

    const glm::vec3 d = p1 - p0;
    float t_min = 0.0f;
    float t_max = 1.0f;
    glm::vec3 hit_normal{0.0f};

    // X-axis
    if (std::abs(d.x) < 1e-7f) {
        if (p0.x < min_x || p0.x > max_x) {
            return false;
        }
    } else {
        const float inv_dx = 1.0f / d.x;
        float t1 = (min_x - p0.x) * inv_dx;
        float t2 = (max_x - p0.x) * inv_dx;
        glm::vec3 n1{-1.0f, 0.0f, 0.0f};
        if (t1 > t2) {
            std::swap(t1, t2);
            n1 = glm::vec3{1.0f, 0.0f, 0.0f};
        }
        if (t1 > t_min) {
            t_min = t1;
            hit_normal = n1;
        }
        t_max = std::min(t_max, t2);
        if (t_min > t_max) {
            return false;
        }
    }

    // Y-axis
    if (std::abs(d.y) < 1e-7f) {
        if (p0.y < min_y || p0.y > max_y) {
            return false;
        }
    } else {
        const float inv_dy = 1.0f / d.y;
        float t1 = (min_y - p0.y) * inv_dy;
        float t2 = (max_y - p0.y) * inv_dy;
        glm::vec3 n1{0.0f, -1.0f, 0.0f};
        if (t1 > t2) {
            std::swap(t1, t2);
            n1 = glm::vec3{0.0f, 1.0f, 0.0f};
        }
        if (t1 > t_min) {
            t_min = t1;
            hit_normal = n1;
        }
        t_max = std::min(t_max, t2);
        if (t_min > t_max) {
            return false;
        }
    }

    if (t_min >= 0.0f && t_min <= 1.0f) {
        hit.hit = true;
        hit.t = t_min;
        hit.point = p0 + d * t_min;
        hit.normal = hit_normal;
        return true;
    }

    return false;
}

bool check_circle_collision(
        const glm::vec3& p0,
        const glm::vec3& p1,
        float radius,
        const glm::vec3& circle_pos,
        float circle_radius,
        HitResult& hit) {
    const float eff_radius = circle_radius + radius;
    const glm::vec2 d{p1.x - p0.x, p1.y - p0.y};
    const glm::vec2 f{p0.x - circle_pos.x, p0.y - circle_pos.y};

    const float c = glm::dot(f, f) - (eff_radius * eff_radius);
    if (c <= 0.0f) {
        // p0 is inside the circle
        const float dist = glm::length(f);
        const glm::vec3 norm = (dist > 1e-6f) ? glm::vec3(f / dist, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        hit.hit = true;
        hit.t = 0.0f;
        hit.point = glm::vec3(circle_pos.x, circle_pos.y, p0.z) + norm * eff_radius;
        hit.normal = norm;
        return true;
    }

    const float a = glm::dot(d, d);
    if (a < 1e-12f) {
        return false;
    }

    const float b = 2.0f * glm::dot(f, d);
    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) {
        return false;
    }

    const float sqrt_disc = std::sqrt(disc);
    const float t_hit = (-b - sqrt_disc) / (2.0f * a);
    if (t_hit >= 0.0f && t_hit <= 1.0f) {
        hit.hit = true;
        hit.t = t_hit;
        hit.point = p0 + glm::vec3(d, 0.0f) * t_hit;
        const glm::vec2 norm_xy = glm::vec2(hit.point.x - circle_pos.x, hit.point.y - circle_pos.y);
        const float len = glm::length(norm_xy);
        hit.normal = (len > 1e-6f) ? glm::vec3(norm_xy / len, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        return true;
    }

    return false;
}

[[nodiscard]] bool finite3(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

[[nodiscard]] glm::quat unit_rotation(glm::quat rotation) {
    const float len = glm::length(rotation);
    if (!std::isfinite(len) || !(len > 1e-8f)) {
        return glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
    }
    return glm::normalize(rotation);
}

// Local AABB slab. `min_b` / `max_b` are the expanded bounds. The hit point and normal stay in this space.
bool slab_aabb(
        const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& min_b, const glm::vec3& max_b, HitResult& hit) {
    if (p0.x >= min_b.x && p0.x <= max_b.x && p0.y >= min_b.y && p0.y <= max_b.y && p0.z >= min_b.z &&
        p0.z <= max_b.z) {
        const float d_left = p0.x - min_b.x;
        const float d_right = max_b.x - p0.x;
        const float d_bottom = p0.y - min_b.y;
        const float d_top = max_b.y - p0.y;
        const float d_near = p0.z - min_b.z;
        const float d_far = max_b.z - p0.z;

        float min_d = d_left;
        glm::vec3 norm{-1.0f, 0.0f, 0.0f};
        if (d_right < min_d) {
            min_d = d_right;
            norm = glm::vec3{1.0f, 0.0f, 0.0f};
        }
        if (d_bottom < min_d) {
            min_d = d_bottom;
            norm = glm::vec3{0.0f, -1.0f, 0.0f};
        }
        if (d_top < min_d) {
            min_d = d_top;
            norm = glm::vec3{0.0f, 1.0f, 0.0f};
        }
        if (d_near < min_d) {
            min_d = d_near;
            norm = glm::vec3{0.0f, 0.0f, -1.0f};
        }
        if (d_far < min_d) {
            norm = glm::vec3{0.0f, 0.0f, 1.0f};
        }

        hit.hit = true;
        hit.t = 0.0f;
        hit.point = p0;
        hit.normal = norm;
        return true;
    }

    const glm::vec3 d = p1 - p0;
    float t_min = 0.0f;
    float t_max = 1.0f;
    glm::vec3 hit_normal{0.0f};

    auto axis = [&](int i, const glm::vec3& n_min) -> bool {
        const float delta = d[i];
        const float p = p0[i];
        const float lo = min_b[i];
        const float hi = max_b[i];
        if (std::abs(delta) < 1e-7f) {
            return p >= lo && p <= hi;
        }
        const float inv = 1.0f / delta;
        float t1 = (lo - p) * inv;
        float t2 = (hi - p) * inv;
        glm::vec3 n1 = n_min;
        if (t1 > t2) {
            std::swap(t1, t2);
            n1 = -n_min;
        }
        if (t1 > t_min) {
            t_min = t1;
            hit_normal = n1;
        }
        t_max = std::min(t_max, t2);
        return t_min <= t_max;
    };

    if (!axis(0, glm::vec3{-1.0f, 0.0f, 0.0f}) || !axis(1, glm::vec3{0.0f, -1.0f, 0.0f}) ||
        !axis(2, glm::vec3{0.0f, 0.0f, -1.0f})) {
        return false;
    }

    if (t_min >= 0.0f && t_min <= 1.0f) {
        hit.hit = true;
        hit.t = t_min;
        hit.point = p0 + d * t_min;
        hit.normal = hit_normal;
        return true;
    }
    return false;
}

bool check_oriented_box_collision(
        const glm::vec3& p0,
        const glm::vec3& p1,
        float radius,
        const glm::vec3& box_pos,
        const glm::quat& rotation,
        const glm::vec3& box_size,
        HitResult& hit) {
    if (!finite3(p0) || !finite3(p1) || !finite3(box_pos) || !finite3(box_size)) {
        return false;
    }
    if (!(box_size.x > 0.0f) || !(box_size.y > 0.0f) || !(box_size.z > 0.0f)) {
        return false;
    }
    const glm::quat q = unit_rotation(rotation);
    const glm::vec3 local0 = glm::conjugate(q) * (p0 - box_pos);
    const glm::vec3 local1 = glm::conjugate(q) * (p1 - box_pos);
    const glm::vec3 half = box_size * 0.5f + glm::vec3{radius};
    HitResult local;
    if (!slab_aabb(local0, local1, -half, half, local)) {
        return false;
    }
    hit.hit = true;
    hit.t = local.t;
    hit.point = p0 + (p1 - p0) * local.t;
    hit.normal = q * local.normal;
    return true;
}

bool check_sphere_collision(
        const glm::vec3& p0,
        const glm::vec3& p1,
        float radius,
        const glm::vec3& center,
        float sphere_radius,
        HitResult& hit) {
    if (!finite3(p0) || !finite3(p1) || !finite3(center) || !(sphere_radius > 0.0f)) {
        return false;
    }
    const float eff_radius = sphere_radius + radius;
    const glm::vec3 d = p1 - p0;
    const glm::vec3 f = p0 - center;
    const float c = glm::dot(f, f) - (eff_radius * eff_radius);
    if (c <= 0.0f) {
        const float dist = glm::length(f);
        const glm::vec3 norm = (dist > 1e-6f) ? (f / dist) : glm::vec3{0.0f, 1.0f, 0.0f};
        hit.hit = true;
        hit.t = 0.0f;
        hit.point = center + norm * eff_radius;
        hit.normal = norm;
        return true;
    }

    const float a = glm::dot(d, d);
    if (a < 1e-12f) {
        return false;
    }
    const float b = 2.0f * glm::dot(f, d);
    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) {
        return false;
    }
    const float t_hit = (-b - std::sqrt(disc)) / (2.0f * a);
    if (t_hit >= 0.0f && t_hit <= 1.0f) {
        hit.hit = true;
        hit.t = t_hit;
        hit.point = p0 + d * t_hit;
        const glm::vec3 outward = hit.point - center;
        const float len = glm::length(outward);
        hit.normal = (len > 1e-6f) ? (outward / len) : glm::vec3{0.0f, 1.0f, 0.0f};
        return true;
    }
    return false;
}

bool check_capsule_collision(
        const glm::vec3& p0,
        const glm::vec3& p1,
        float radius,
        const glm::vec3& center,
        const glm::quat& rotation,
        float capsule_radius,
        float height,
        HitResult& hit) {
    if (!finite3(center) || !(capsule_radius > 0.0f) || height < 0.0f) {
        return false;
    }
    if (!(height > 0.0f)) {
        return check_sphere_collision(p0, p1, radius, center, capsule_radius, hit);
    }

    const glm::quat q = unit_rotation(rotation);
    glm::vec3 axis = q * glm::vec3{0.0f, 1.0f, 0.0f};
    const float axis_len = glm::length(axis);
    if (!(axis_len > 1e-8f)) {
        axis = glm::vec3{0.0f, 1.0f, 0.0f};
    } else {
        axis /= axis_len;
    }
    const glm::vec3 a0 = center - axis * (height * 0.5f);
    const glm::vec3 a1 = center + axis * (height * 0.5f);
    const glm::vec3 ab = a1 - a0;
    const float re = capsule_radius + radius;
    const float ab_len2 = glm::dot(ab, ab);

    auto closest_on_core = [&](const glm::vec3& p) {
        if (ab_len2 < 1e-12f) {
            return a0;
        }
        const float s = glm::clamp(glm::dot(p - a0, ab) / ab_len2, 0.0f, 1.0f);
        return a0 + ab * s;
    };

    const glm::vec3 core0 = closest_on_core(p0);
    const glm::vec3 from_core = p0 - core0;
    if (glm::dot(from_core, from_core) <= re * re) {
        const float dist = glm::length(from_core);
        const glm::vec3 norm = (dist > 1e-6f) ? (from_core / dist) : glm::vec3{0.0f, 1.0f, 0.0f};
        hit.hit = true;
        hit.t = 0.0f;
        hit.point = core0 + norm * re;
        hit.normal = norm;
        return true;
    }

    const glm::vec3 d = p1 - p0;
    float best_t = 2.0f;
    glm::vec3 best_normal{0.0f};
    glm::vec3 best_point{0.0f};
    bool found = false;
    auto consider = [&](float t, const glm::vec3& normal, const glm::vec3& point) {
        if (t >= 0.0f && t <= 1.0f && t < best_t) {
            best_t = t;
            best_normal = normal;
            best_point = point;
            found = true;
        }
    };

    const glm::vec3 d_perp = d - axis * glm::dot(d, axis);
    const glm::vec3 f_perp = (p0 - a0) - axis * glm::dot(p0 - a0, axis);
    const float a = glm::dot(d_perp, d_perp);
    if (a >= 1e-12f) {
        const float b = 2.0f * glm::dot(f_perp, d_perp);
        const float c = glm::dot(f_perp, f_perp) - re * re;
        const float disc = b * b - 4.0f * a * c;
        if (disc >= 0.0f) {
            const float t_hit = (-b - std::sqrt(disc)) / (2.0f * a);
            if (t_hit >= 0.0f && t_hit <= 1.0f && ab_len2 >= 1e-12f) {
                const glm::vec3 point = p0 + d * t_hit;
                const float s = glm::dot(point - a0, ab) / ab_len2;
                if (s >= 0.0f && s <= 1.0f) {
                    const glm::vec3 core = a0 + ab * s;
                    const glm::vec3 outward = point - core;
                    const float len = glm::length(outward);
                    if (len > 1e-6f) {
                        consider(t_hit, outward / len, point);
                    }
                }
            }
        }
    }

    HitResult cap;
    if (check_sphere_collision(p0, p1, radius, a0, capsule_radius, cap)) {
        consider(cap.t, cap.normal, cap.point);
    }
    if (check_sphere_collision(p0, p1, radius, a1, capsule_radius, cap)) {
        consider(cap.t, cap.normal, cap.point);
    }
    if (!found) {
        return false;
    }
    hit.hit = true;
    hit.t = best_t;
    hit.normal = best_normal;
    hit.point = best_point;
    return true;
}

// One particle, one step. False means it died and the caller drops it after every index has been stepped.
// Reads the emitter and the collider spans; writes only `particle`.
struct ParticleStep {
    const ParticleEmitter* emitter = nullptr;
    float dt = 0.0f;
    const glm::mat4* transform = nullptr;
    // Set when the emitter simulates in local space and collision is on. Otherwise null.
    const glm::mat4* inverse = nullptr;
    std::span<const ParticleCollider2D> colliders;
    std::span<const ParticleCollider3D> colliders_3d;
};

constexpr std::size_t kParticleGrain = 64;

[[nodiscard]] bool step_particle(Particle& p, const ParticleStep& step) {
    const ParticleEmitter& emitter = *step.emitter;
    const float dt = step.dt;
    p.age += dt;
    if (p.age >= p.lifetime) {
        return false;
    }

    const float t = glm::clamp(p.age / p.lifetime, 0.0f, 1.0f);
    const glm::vec3 prev_pos = p.position;
    p.velocity += emitter.gravity * dt;
    p.rotation += p.angular_velocity * dt;
    const glm::vec3 next_pos = prev_pos + p.velocity * dt;

    if (emitter.collision_enabled && (!step.colliders.empty() || !step.colliders_3d.empty())) {
        glm::vec3 seg_p0 = prev_pos;
        glm::vec3 seg_p1 = next_pos;
        if (emitter.simulation_space == SimulationSpace::Local) {
            const glm::mat4& transform = *step.transform;
            seg_p0 = glm::vec3(transform * glm::vec4(prev_pos, 1.0f));
            seg_p1 = glm::vec3(transform * glm::vec4(next_pos, 1.0f));
        }

        HitResult closest_hit;
        for (const ParticleCollider2D& col : step.colliders) {
            if ((col.layer & emitter.collision_mask) == 0) {
                continue;
            }

            HitResult candidate_hit;
            bool collided = false;
            if (col.shape == ParticleCollider2D::Shape::Box) {
                collided = check_box_collision(
                        seg_p0, seg_p1, emitter.collision_radius, col.position, col.box_size, candidate_hit);
            } else if (col.shape == ParticleCollider2D::Shape::Circle) {
                collided = check_circle_collision(
                        seg_p0, seg_p1, emitter.collision_radius, col.position, col.circle_radius, candidate_hit);
            }

            if (collided && (!closest_hit.hit || candidate_hit.t < closest_hit.t)) {
                closest_hit = candidate_hit;
            }
        }

        for (const ParticleCollider3D& col : step.colliders_3d) {
            if ((col.layer & emitter.collision_mask) == 0) {
                continue;
            }

            HitResult candidate_hit;
            bool collided = false;
            if (col.shape == ParticleCollider3D::Shape::Box) {
                collided = check_oriented_box_collision(
                        seg_p0, seg_p1, emitter.collision_radius, col.position, col.rotation, col.box_size,
                        candidate_hit);
            } else if (col.shape == ParticleCollider3D::Shape::Sphere) {
                collided = check_sphere_collision(
                        seg_p0, seg_p1, emitter.collision_radius, col.position, col.radius, candidate_hit);
            } else if (col.shape == ParticleCollider3D::Shape::Capsule) {
                collided = check_capsule_collision(
                        seg_p0, seg_p1, emitter.collision_radius, col.position, col.rotation, col.radius, col.height,
                        candidate_hit);
            }

            if (collided && (!closest_hit.hit || candidate_hit.t < closest_hit.t)) {
                closest_hit = candidate_hit;
            }
        }

        if (closest_hit.hit) {
            if (emitter.kill_on_collision) {
                p.age = p.lifetime;
                return false;
            }

            glm::vec3 hit_normal = closest_hit.normal;
            glm::vec3 hit_point = closest_hit.point;
            if (emitter.simulation_space == SimulationSpace::Local) {
                const glm::mat4& transform = *step.transform;
                const glm::mat4& inverse = *step.inverse;
                hit_point = glm::vec3(inverse * glm::vec4(closest_hit.point, 1.0f));
                hit_normal = glm::normalize(
                        glm::vec3(glm::transpose(transform) * glm::vec4(closest_hit.normal, 0.0f)));
            }

            constexpr float epsilon = 0.001f;
            p.position = hit_point + hit_normal * epsilon;

            const float vn = glm::dot(p.velocity, hit_normal);
            if (vn < 0.0f) {
                const glm::vec3 v_normal = vn * hit_normal;
                const glm::vec3 v_tangent = p.velocity - v_normal;
                const float clamped_bounce = glm::clamp(emitter.bounce, 0.0f, 1.0f);
                const float clamped_friction = glm::clamp(emitter.friction, 0.0f, 1.0f);
                p.velocity = (-clamped_bounce * v_normal) + ((1.0f - clamped_friction) * v_tangent);
            }

            const float remaining_dt = dt * (1.0f - closest_hit.t);
            if (remaining_dt > 0.0f) {
                p.position += p.velocity * remaining_dt;
            }

            if (emitter.lifetime_loss > 0.0f) {
                p.age += p.lifetime * glm::clamp(emitter.lifetime_loss, 0.0f, 1.0f);
                if (p.age >= p.lifetime) {
                    return false;
                }
            }
        } else {
            p.position = next_pos;
        }
    } else {
        p.position = next_pos;
    }

    if (!emitter.color_curve.empty()) {
        p.color = emitter.color_curve.evaluate(t);
    } else {
        p.color = glm::mix(p.start_color, p.end_color, t);
    }
    if (!emitter.alpha_curve.empty()) {
        p.color.a = emitter.alpha_curve.evaluate(t);
    }

    if (!emitter.size_curve_xy.empty()) {
        p.size = p.base_size * emitter.size_curve_xy.evaluate(t);
    } else if (!emitter.size_curve.empty()) {
        p.size = p.base_size * emitter.size_curve.evaluate(t);
    } else {
        p.size = glm::mix(p.start_size, p.end_size, t);
    }
    return true;
}

// Caller-only. The predicate calls game code, so this stays off the integrate job.
void sweep_particle_grid(
        ParticleEmitter& emitter,
        const glm::mat4& transform,
        const glm::mat4* inverse,
        const ParticleGrid& grid,
        const std::vector<glm::vec3>& starts,
        std::vector<char>& alive) {
    const bool local = emitter.simulation_space == SimulationSpace::Local;
    float grid_radius = emitter.collision_radius;
    if (!(grid_radius > 0.0f)) {
        grid_radius = 0.0f;
    }
    if (grid_radius == 0.0f && emitter.draw == ParticleDraw::Mesh) {
        grid_radius = 0.5f;
    }
    const glm::vec3 half{grid_radius};
    auto is_solid = [&grid](glm::ivec3 cell) { return grid.is_solid(grid.data, cell); };

    for (std::size_t i = 0; i < emitter.particles.size(); ++i) {
        if (alive[i] == 0) {
            continue;
        }
        Particle& p = emitter.particles[i];
        glm::vec3 start = starts[i];
        glm::vec3 end = p.position;
        if (local) {
            start = glm::vec3(transform * glm::vec4(start, 1.0f));
            end = glm::vec3(transform * glm::vec4(end, 1.0f));
        }
        const Aabb box{start - half, start + half};
        const GridSweep swept = sweep_aabb(box, end - start, is_solid);
        if (swept.hit == glm::ivec3{0}) {
            continue;
        }
        glm::vec3 normal{0.0f};
        if (swept.hit.x != 0) {
            normal.x = -static_cast<float>(swept.hit.x);
        }
        if (swept.hit.y != 0) {
            normal.y = -static_cast<float>(swept.hit.y);
        }
        if (swept.hit.z != 0) {
            normal.z = -static_cast<float>(swept.hit.z);
        }
        if (!(glm::dot(normal, normal) > 1e-12f)) {
            continue;
        }
        normal = glm::normalize(normal);
        glm::vec3 center = (swept.box.min + swept.box.max) * 0.5f;
        if (local && inverse != nullptr) {
            center = glm::vec3((*inverse) * glm::vec4(center, 1.0f));
            normal = glm::normalize(glm::vec3(glm::transpose(transform) * glm::vec4(normal, 0.0f)));
        }
        if (emitter.kill_on_collision) {
            p.age = p.lifetime;
            alive[i] = 0;
            continue;
        }
        p.position = center;
        const float vn = glm::dot(p.velocity, normal);
        if (vn < 0.0f) {
            const glm::vec3 v_normal = vn * normal;
            const glm::vec3 v_tangent = p.velocity - v_normal;
            const float clamped_bounce = glm::clamp(emitter.bounce, 0.0f, 1.0f);
            const float clamped_friction = glm::clamp(emitter.friction, 0.0f, 1.0f);
            p.velocity = (-clamped_bounce * v_normal) + ((1.0f - clamped_friction) * v_tangent);
        }
        if (emitter.lifetime_loss > 0.0f) {
            p.age += p.lifetime * glm::clamp(emitter.lifetime_loss, 0.0f, 1.0f);
            if (p.age >= p.lifetime) {
                alive[i] = 0;
            }
        }
    }
}

}

namespace {

glm::vec3 safe_axis(glm::vec3 v, glm::vec3 fallback) {
    const float len = glm::length(v);
    if (!(len > kParticleAxisEpsilon)) {
        return fallback;
    }
    return v / len;
}

} // namespace

glm::vec3 particle_card_offset(
        ParticleDraw draw,
        glm::vec3 camera_right,
        glm::vec3 camera_up,
        glm::vec2 corner,
        glm::vec2 size,
        float rotation,
        glm::vec3 velocity) noexcept {
    const glm::vec2 scaled = corner * size;
    const float c = std::cos(rotation);
    const float s = std::sin(rotation);
    const glm::vec2 rotated{scaled.x * c - scaled.y * s, scaled.x * s + scaled.y * c};
    if (draw == ParticleDraw::Plane) {
        return {rotated.x, rotated.y, 0.0f};
    }

    const glm::vec3 right = safe_axis(camera_right, {1.0f, 0.0f, 0.0f});
    const glm::vec3 up = safe_axis(camera_up, {0.0f, 1.0f, 0.0f});
    glm::vec3 axis_x = right;
    glm::vec3 axis_y = up;
    if (draw == ParticleDraw::Stretched) {
        const glm::vec3 normal_raw = glm::cross(right, up);
        const float normal_len = glm::length(normal_raw);
        if (normal_len > kParticleAxisEpsilon) {
            const glm::vec3 normal = normal_raw / normal_len;
            const glm::vec3 planar = right * glm::dot(velocity, right) + up * glm::dot(velocity, up);
            const float planar_len = glm::length(planar);
            if (planar_len > kParticleStretchMinSpeed) {
                axis_y = planar / planar_len;
                axis_x = glm::cross(axis_y, normal);
            }
        }
    }
    return axis_x * rotated.x + axis_y * rotated.y;
}

float particle_mesh_size_factor(const Curve<float>& size_curve, float age, float lifetime) noexcept {
    if (size_curve.empty()) {
        return 1.0f;
    }
    const float t = lifetime > 0.0f ? glm::clamp(age / lifetime, 0.0f, 1.0f) : 0.0f;
    return size_curve.evaluate(t);
}

MeshParticleInstance particle_mesh_instance(
        glm::vec3 position, glm::vec3 mesh_scale, float size_factor, glm::vec4 color, float layer) noexcept {
    return MeshParticleInstance{
            .position = position,
            .scale = mesh_scale * size_factor,
            .color = color,
            .layer = layer,
    };
}

void update_emitter(
        ParticleEmitter& emitter,
        float dt,
        const glm::mat4& transform,
        std::span<const ParticleCollider2D> colliders,
        std::span<const ParticleCollider3D> colliders_3d,
        JobSystem* jobs,
        const ParticleGrid* grid) {
    if (dt <= 0.0f) {
        return;
    }

    // 1. Integrate every particle, then drop the dead ones. Survivors keep their order.
    const std::size_t count = emitter.particles.size();
    if (count > 0) {
        const bool shape_collide = emitter.collision_enabled && (!colliders.empty() || !colliders_3d.empty());
        const bool grid_collide = emitter.collision_enabled && emitter.collision_grid && grid != nullptr &&
                                  grid->is_solid != nullptr;
        glm::mat4 inverse{1.0f};
        const glm::mat4* inverse_ptr = nullptr;
        if ((shape_collide || grid_collide) && emitter.simulation_space == SimulationSpace::Local) {
            inverse = glm::inverse(transform);
            inverse_ptr = &inverse;
        }
        const ParticleStep step{
                .emitter = &emitter,
                .dt = dt,
                .transform = &transform,
                .inverse = inverse_ptr,
                .colliders = colliders,
                .colliders_3d = colliders_3d,
        };
        std::vector<glm::vec3> grid_starts;
        if (grid_collide) {
            grid_starts.resize(count);
            for (std::size_t i = 0; i < count; ++i) {
                grid_starts[i] = emitter.particles[i].position;
            }
        }
        std::vector<char> alive(count, 1);
        auto integrate = [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                alive[i] = step_particle(emitter.particles[i], step) ? 1 : 0;
            }
        };
        if (jobs != nullptr) {
            jobs->parallel_for(count, kParticleGrain, integrate);
        } else {
            integrate(0, count);
        }
        if (grid_collide) {
            sweep_particle_grid(emitter, transform, inverse_ptr, *grid, grid_starts, alive);
        }

        std::size_t write = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (alive[i] == 0) {
                continue;
            }
            if (write != i) {
                emitter.particles[write] = std::move(emitter.particles[i]);
            }
            ++write;
        }
        emitter.particles.resize(write);
    }


    // 2. Check emission status
    if (!emitter.playing) {
        return;
    }

    if (emitter.duration > 0.0f) {
        emitter.elapsed_time += dt;
        if (emitter.elapsed_time >= emitter.duration) {
            if (emitter.looping) {
                emitter.elapsed_time = std::fmod(emitter.elapsed_time, emitter.duration);
            } else {
                emitter.playing = false;
            }
        }
    }

    // 3. Determine number of particles to spawn
    int spawn_count = emitter.pending_burst;
    emitter.pending_burst = 0;

    if (emitter.playing && emitter.emission_rate > 0.0f) {
        emitter.emission_accumulator += emitter.emission_rate * dt;
        const int continuous = static_cast<int>(emitter.emission_accumulator);
        spawn_count += continuous;
        emitter.emission_accumulator -= static_cast<float>(continuous);
    }

    if (spawn_count <= 0) {
        return;
    }

    const std::size_t current_count = emitter.particles.size();
    if (current_count >= emitter.max_particles) {
        return;
    }

    const int available_capacity = static_cast<int>(emitter.max_particles - current_count);
    spawn_count = std::min(spawn_count, available_capacity);

    // 4. Spawn new particles
    emitter.particles.reserve(current_count + static_cast<std::size_t>(spawn_count));

    for (int i = 0; i < spawn_count; ++i) {
        Particle p;
        p.lifetime = rand_range(emitter.lifetime_min, emitter.lifetime_max);
        p.age = 0.0f;

        const float speed = rand_range(emitter.speed_min, emitter.speed_max);
        const glm::vec3 local_dir = compute_direction(emitter.direction, emitter.spread_angle);
        const glm::vec3 local_offset = compute_shape_offset(emitter.shape, emitter.shape_scale, emitter.direction);
        const glm::vec3 local_velocity = local_dir * speed;

        if (emitter.simulation_space == SimulationSpace::World) {
            p.position = glm::vec3(transform * glm::vec4(local_offset, 1.0f));
            p.velocity = glm::mat3(transform) * local_velocity;
        } else {
            p.position = local_offset;
            p.velocity = local_velocity;
        }

        p.rotation = rand_range(emitter.rotation_min, emitter.rotation_max);
        p.angular_velocity = rand_range(emitter.angular_velocity_min, emitter.angular_velocity_max);

        p.base_size = rand_vec2(emitter.size_start_min, emitter.size_start_max);
        p.start_size = p.base_size;
        p.end_size = rand_vec2(emitter.size_end_min, emitter.size_end_max);

        if (!emitter.size_curve_xy.empty()) {
            p.size = p.base_size * emitter.size_curve_xy.evaluate(0.0f);
        } else if (!emitter.size_curve.empty()) {
            p.size = p.base_size * emitter.size_curve.evaluate(0.0f);
        } else {
            p.size = p.start_size;
        }

        if (!emitter.color_curve.empty()) {
            p.color = emitter.color_curve.evaluate(0.0f);
        } else {
            p.start_color = emitter.color_start;
            p.end_color = emitter.color_end;
            p.color = p.start_color;
        }

        if (!emitter.alpha_curve.empty()) {
            p.color.a = emitter.alpha_curve.evaluate(0.0f);
        }

        p.texture_layer = emitter.texture_layer;

        emitter.particles.push_back(std::move(p));
    }
}

}
