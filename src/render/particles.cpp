#include <engine/render/particles.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <random>

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

}

void update_emitter(ParticleEmitter& emitter, float dt, const glm::mat4& transform) {
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
        } else {
            const float t = glm::clamp(p.age / p.lifetime, 0.0f, 1.0f);
            p.velocity += emitter.gravity * dt;
            p.position += p.velocity * dt;
            p.rotation += p.angular_velocity * dt;
            p.color = glm::mix(p.start_color, p.end_color, t);
            p.size = glm::mix(p.start_size, p.end_size, t);
            ++i;
        }
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

        p.start_size = rand_vec2(emitter.size_start_min, emitter.size_start_max);
        p.end_size = rand_vec2(emitter.size_end_min, emitter.size_end_max);
        p.size = p.start_size;

        p.start_color = emitter.color_start;
        p.end_color = emitter.color_end;
        p.color = p.start_color;

        emitter.particles.push_back(std::move(p));
    }
}

}
