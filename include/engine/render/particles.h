#pragma once

#include <engine/ecs/entity.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace engine::render {

enum class EmitterShape {
    Point,
    Box,
    Circle,
    Cone,
};

enum class SimulationSpace {
    World,
    Local,
};

struct Particle {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec2 size{1.0f, 1.0f};
    float rotation = 0.0f;
    float angular_velocity = 0.0f;
    float age = 0.0f;
    float lifetime = 1.0f;
    glm::vec2 start_size{1.0f, 1.0f};
    glm::vec2 end_size{0.0f, 0.0f};
    glm::vec4 start_color{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 end_color{1.0f, 1.0f, 1.0f, 0.0f};
};

struct ParticleEmitter {
    // Emission parameters
    float emission_rate = 10.0f;          // particles spawned per second
    std::size_t max_particles = 1000;     // maximum capacity of active particles
    float duration = 0.0f;                // duration in seconds (<= 0 means infinite loop)
    bool looping = true;
    bool playing = true;

    // Lifetime & Speed
    float lifetime_min = 1.0f;
    float lifetime_max = 1.0f;
    float speed_min = 1.0f;
    float speed_max = 1.0f;
    glm::vec3 direction{0.0f, 1.0f, 0.0f};
    float spread_angle = 0.0f;            // spread half-angle in radians for direction/cone

    // Size & Color over lifetime
    glm::vec2 size_start_min{1.0f, 1.0f};
    glm::vec2 size_start_max{1.0f, 1.0f};
    glm::vec2 size_end_min{0.0f, 0.0f};
    glm::vec2 size_end_max{0.0f, 0.0f};
    glm::vec4 color_start{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 color_end{1.0f, 1.0f, 1.0f, 0.0f};

    // Rotation (radians)
    float rotation_min = 0.0f;
    float rotation_max = 0.0f;
    float angular_velocity_min = 0.0f;
    float angular_velocity_max = 0.0f;

    // Physics forces & space
    glm::vec3 gravity{0.0f, 0.0f, 0.0f};
    EmitterShape shape = EmitterShape::Point;
    glm::vec3 shape_scale{1.0f, 1.0f, 1.0f};
    SimulationSpace simulation_space = SimulationSpace::World;

    // Rendering configuration
    std::shared_ptr<ITexture> texture;
    std::shared_ptr<IMaterial> material;
    std::shared_ptr<IMesh> mesh;
    BlendMode blend = BlendMode::Alpha;
    int layer = 0;
    int order_in_layer = 0;
    glm::vec2 uv_scale{1.0f, 1.0f};
    glm::vec2 uv_offset{0.0f, 0.0f};

    // Runtime state
    std::vector<Particle> particles;
    float emission_accumulator = 0.0f;
    float elapsed_time = 0.0f;
    int pending_burst = 0;

    void play() noexcept {
        playing = true;
        elapsed_time = 0.0f;
    }

    void pause() noexcept {
        playing = false;
    }

    void stop() noexcept {
        playing = false;
        elapsed_time = 0.0f;
        emission_accumulator = 0.0f;
        pending_burst = 0;
        particles.clear();
    }

    void burst(int count) noexcept {
        if (count > 0) {
            pending_burst += count;
        }
    }

    [[nodiscard]] std::size_t active_count() const noexcept {
        return particles.size();
    }
};

void update_emitter(ParticleEmitter& emitter, float dt, const glm::mat4& transform = glm::mat4{1.0f});

}
