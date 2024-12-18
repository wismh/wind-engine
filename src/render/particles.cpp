#include <engine/render/particles.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <span>

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

glm::vec3 compute_shape_offset(EmitterShape shape, const glm::vec3& scale) {
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

}

void update_emitter(
        ParticleEmitter& emitter,
        float dt,
        const glm::mat4& transform,
        std::span<const ParticleCollider> colliders) {
    if (dt <= 0.0f) {
        return;
    }

    // 1. Update existing particles
    for (std::size_t i = 0; i < emitter.particles.size();) {
        Particle& p = emitter.particles[i];
        p.age += dt;
        if (p.age >= p.lifetime) {
            if (i + 1 < emitter.particles.size()) {
                emitter.particles[i] = std::move(emitter.particles.back());
            }
            emitter.particles.pop_back();
            continue;
        }

        const float t = glm::clamp(p.age / p.lifetime, 0.0f, 1.0f);
        const glm::vec3 prev_pos = p.position;
        p.velocity += emitter.gravity * dt;
        p.rotation += p.angular_velocity * dt;
        const glm::vec3 next_pos = prev_pos + p.velocity * dt;

        if (emitter.collision_enabled && !colliders.empty()) {
            glm::vec3 seg_p0 = prev_pos;
            glm::vec3 seg_p1 = next_pos;
            if (emitter.simulation_space == SimulationSpace::Local) {
                seg_p0 = glm::vec3(transform * glm::vec4(prev_pos, 1.0f));
                seg_p1 = glm::vec3(transform * glm::vec4(next_pos, 1.0f));
            }

            HitResult closest_hit;
            for (const auto& col : colliders) {
                if ((col.layer & emitter.collision_mask) == 0) {
                    continue;
                }

                HitResult candidate_hit;
                bool collided = false;
                if (col.shape == ParticleCollider::Shape::Box) {
                    collided = check_box_collision(
                            seg_p0,
                            seg_p1,
                            emitter.collision_radius,
                            col.position,
                            col.box_size,
                            candidate_hit);
                } else if (col.shape == ParticleCollider::Shape::Circle) {
                    collided = check_circle_collision(
                            seg_p0,
                            seg_p1,
                            emitter.collision_radius,
                            col.position,
                            col.circle_radius,
                            candidate_hit);
                }

                if (collided && (!closest_hit.hit || candidate_hit.t < closest_hit.t)) {
                    closest_hit = candidate_hit;
                }
            }

            if (closest_hit.hit) {
                if (emitter.kill_on_collision) {
                    p.age = p.lifetime;
                    if (i + 1 < emitter.particles.size()) {
                        emitter.particles[i] = std::move(emitter.particles.back());
                    }
                    emitter.particles.pop_back();
                    continue;
                }

                glm::vec3 hit_normal = closest_hit.normal;
                glm::vec3 hit_point = closest_hit.point;
                if (emitter.simulation_space == SimulationSpace::Local) {
                    const glm::mat4 inv_transform = glm::inverse(transform);
                    hit_point = glm::vec3(inv_transform * glm::vec4(closest_hit.point, 1.0f));
                    hit_normal = glm::normalize(glm::vec3(glm::transpose(transform) * glm::vec4(closest_hit.normal, 0.0f)));
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
                        if (i + 1 < emitter.particles.size()) {
                            emitter.particles[i] = std::move(emitter.particles.back());
                        }
                        emitter.particles.pop_back();
                        continue;
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
            ++i;
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
        const glm::vec3 local_offset = compute_shape_offset(emitter.shape, emitter.shape_scale);
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

        emitter.particles.push_back(std::move(p));
    }
}

}
