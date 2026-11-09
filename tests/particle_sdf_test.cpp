#include <gtest/gtest.h>

#include <engine/core/job_system.h>
#include <engine/core/time.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/sdf_2d.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/render/particles.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

// LQ-25: particles collide with SdfCollider2D terrain.

namespace {

constexpr float kCell = 0.25f;
constexpr float kBand = 1.0f;
// The ground's top surface.
constexpr float kGround = 0.5f;
constexpr float kDt = 1.0f / 60.0f;

using engine::render::Particle;
using engine::render::ParticleCollider2D;
using engine::render::ParticleEmitter;

// Solid below y = kGround over x in [-6, 6], three units deep.
engine::SdfField2D ground_field() {
    engine::SdfField2D field{engine::SdfField2DDesc{.cell_size = kCell, .band = kBand}};
    const std::array<glm::vec2, 4> ground{
            glm::vec2{-6.f, -3.f}, glm::vec2{6.f, -3.f}, glm::vec2{6.f, kGround}, glm::vec2{-6.f, kGround}};
    (void)field.fill_polygon(ground);
    return field;
}

// A solid world with a round cavity of `radius` at the origin.
engine::SdfField2D cavity_field(float radius = 3.0f) {
    engine::SdfField2D field{
            engine::SdfField2DDesc{.cell_size = kCell, .band = kBand, .outside = engine::SdfOutside::Solid}};
    (void)field.carve_circle({0.f, 0.f}, radius);
    return field;
}

// No spawning: the tests place every particle themselves.
ParticleEmitter collision_emitter(glm::vec3 gravity = {0.f, 0.f, 0.f}) {
    ParticleEmitter emitter;
    emitter.emission_rate = 0.0f;
    emitter.playing = false;
    emitter.gravity = gravity;
    emitter.collision_enabled = true;
    emitter.bounce = 0.6f;
    emitter.friction = 0.0f;
    return emitter;
}

Particle particle_at(glm::vec2 position, glm::vec2 velocity) {
    Particle p;
    p.position = {position, 0.f};
    p.velocity = {velocity, 0.f};
    p.lifetime = 1000.0f;
    return p;
}

ParticleCollider2D sdf_collider(const engine::SdfField2D& field, uint32_t layer = 1, glm::vec2 position = {0.f, 0.f},
                                float angle = 0.f) {
    return ParticleCollider2D{
            .shape = ParticleCollider2D::Shape::Sdf,
            .position = {position, 0.f},
            .box_size = glm::vec3{0.f},
            .circle_radius = 0.f,
            .layer = layer,
            .field = &field,
            .angle = angle,
    };
}

void step(ParticleEmitter& emitter, std::span<const ParticleCollider2D> colliders, int steps = 1,
          engine::JobSystem* jobs = nullptr, const glm::mat4& transform = glm::mat4{1.0f}) {
    for (int i = 0; i < steps; ++i) {
        engine::render::update_emitter(emitter, kDt, transform, colliders, {}, jobs);
    }
}

// Many particles inside the cavity, moving in a fixed pattern of directions.
void fill_cavity(ParticleEmitter& emitter, int side = 20) {
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const glm::vec2 p{-1.9f + 3.8f * static_cast<float>(x) / static_cast<float>(side - 1),
                              -1.9f + 3.8f * static_cast<float>(y) / static_cast<float>(side - 1)};
            if (glm::length(p) > 2.5f) {
                continue;
            }
            const float a = static_cast<float>(x * 7 + y * 13);
            emitter.particles.push_back(particle_at(p, {6.f * std::cos(a), 6.f * std::sin(a)}));
        }
    }
}

engine::ecs::Entity add_terrain(engine::ecs::World& world, engine::SdfField2D field, uint32_t layer = 1,
                                engine::Transform transform = engine::Transform{}) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, transform);
    world.emplace<engine::SdfCollider2D>(entity, engine::SdfCollider2D{.field = std::move(field), .layer = layer});
    return entity;
}

// An emitter entity at the origin holding one particle; returns the entity.
engine::ecs::Entity add_emitter(engine::ecs::World& world, Particle particle, uint32_t mask = 0xFFFFFFFFu) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{});
    ParticleEmitter emitter = collision_emitter();
    emitter.collision_mask = mask;
    emitter.particles.push_back(particle);
    world.emplace<ParticleEmitter>(entity, std::move(emitter));
    return entity;
}

void run_frames(engine::ecs::World& world, int frames) {
    world.ctx<engine::Time>().delta_time = kDt;
    for (int i = 0; i < frames; ++i) {
        engine::run_particles(world);
    }
}

const Particle& only_particle(const engine::ecs::World& world, engine::ecs::Entity emitter) {
    const ParticleEmitter& e = world.get<ParticleEmitter>(emitter);
    EXPECT_EQ(e.active_count(), 1u);
    return e.particles.front();
}

}

TEST(ParticleSdf, AllocatedBoundsCoverTheChunksAndOneCell) {
    const engine::SdfField2D empty{engine::SdfField2DDesc{.cell_size = kCell, .band = kBand}};
    EXPECT_FALSE(empty.allocated_bounds().has_value());

    const engine::SdfField2D field = ground_field();
    const std::optional<engine::SdfBounds2D> bounds = field.allocated_bounds();
    ASSERT_TRUE(bounds.has_value());
    // Every chunk the polygon touched lies inside, plus one cell.
    for (const glm::ivec2 chunk : field.chunk_coords()) {
        const glm::vec2 lo = field.origin() + glm::vec2(chunk * engine::kSdfChunkCells) * kCell;
        const glm::vec2 hi = lo + glm::vec2(static_cast<float>(engine::kSdfChunkCells) * kCell);
        EXPECT_LE(bounds->min.x, lo.x - kCell + 1e-5f);
        EXPECT_LE(bounds->min.y, lo.y - kCell + 1e-5f);
        EXPECT_GE(bounds->max.x, hi.x + kCell - 1e-5f);
        EXPECT_GE(bounds->max.y, hi.y + kCell - 1e-5f);
    }
    // Beyond it the field reads `outside`.
    EXPECT_FLOAT_EQ(field.sample({bounds->max.x + 0.01f, 0.f}), kBand);
    EXPECT_FLOAT_EQ(field.sample({0.f, bounds->min.y - 0.01f}), kBand);
}

TEST(ParticleSdf, ParticlesInACarvedCavityNeverEndInsideTheSolid) {
    const engine::SdfField2D field = cavity_field();
    const std::vector<ParticleCollider2D> colliders{sdf_collider(field)};
    for (const float radius : {0.0f, 0.05f}) {
        ParticleEmitter emitter = collision_emitter({0.f, -10.f, 0.f});
        emitter.bounce = 0.3f;
        emitter.friction = 0.1f;
        emitter.collision_radius = radius;
        fill_cavity(emitter);
        const std::size_t count = emitter.active_count();
        ASSERT_GT(count, 200u);
        for (int s = 0; s < 300; ++s) {
            step(emitter, colliders);
            ASSERT_EQ(emitter.active_count(), count);
            for (const Particle& p : emitter.particles) {
                ASSERT_GE(field.sample(glm::vec2(p.position)), 0.0f) << "radius " << radius << " step " << s;
            }
        }
        // Gravity pools them at the bottom of the cavity.
        for (const Particle& p : emitter.particles) {
            EXPECT_LT(p.position.y, -1.5f);
            EXPECT_GE(field.sample(glm::vec2(p.position)), radius * 0.5f);
        }
    }
}

TEST(ParticleSdf, BounceOffAFlatFloorMatchesRestitutionAndFriction) {
    const engine::SdfField2D field = ground_field();
    const std::vector<ParticleCollider2D> colliders{sdf_collider(field)};
    // 5 units per second steps a third of a cell into the floor (projected); 30 steps two cells (swept).
    for (const float speed : {5.f, 30.f}) {
        ParticleEmitter emitter = collision_emitter();
        emitter.bounce = 0.6f;
        emitter.friction = 0.25f;
        emitter.particles.push_back(particle_at({0.f, 2.f}, {2.f, -speed}));

        int steps = 0;
        while (emitter.particles[0].velocity.y < 0.f && steps < 120) {
            step(emitter, colliders);
            ++steps;
            EXPECT_GE(emitter.particles[0].position.y, kGround) << speed;
        }
        ASSERT_LT(steps, 120) << speed;
        EXPECT_NEAR(emitter.particles[0].velocity.y, 0.6f * speed, 1e-3f * speed);
        EXPECT_NEAR(emitter.particles[0].velocity.x, 0.75f * 2.f, 1e-3f);
    }
}

TEST(ParticleSdf, CollisionRadiusHoldsTheCenterOffTheSurface) {
    const engine::SdfField2D field = ground_field();
    const std::vector<ParticleCollider2D> colliders{sdf_collider(field)};
    ParticleEmitter emitter = collision_emitter({0.f, -10.f, 0.f});
    emitter.bounce = 0.0f;
    emitter.collision_radius = 0.2f;
    emitter.particles.push_back(particle_at({0.f, 2.f}, {0.f, 0.f}));
    step(emitter, colliders, 120);
    EXPECT_NEAR(emitter.particles[0].position.y, kGround + 0.2f, 0.01f);
    EXPECT_GE(emitter.particles[0].position.y, kGround + 0.2f - 1e-4f);
}

TEST(ParticleSdf, FastParticleDoesNotTunnelThroughAThinWall) {
    engine::SdfField2D field{engine::SdfField2DDesc{.cell_size = kCell, .band = kBand}};
    const std::array<glm::vec2, 4> wall{glm::vec2{0.f, -5.f}, glm::vec2{0.5f, -5.f}, glm::vec2{0.5f, 5.f},
                                        glm::vec2{0.f, 5.f}};
    (void)field.fill_polygon(wall);
    const std::vector<ParticleCollider2D> colliders{sdf_collider(field)};
    ParticleEmitter emitter = collision_emitter();
    emitter.bounce = 1.0f;
    // 4 units per step, eight times the wall.
    emitter.particles.push_back(particle_at({-1.f, 0.f}, {240.f, 0.f}));
    step(emitter, colliders);
    EXPECT_LT(emitter.particles[0].position.x, 0.f);
    EXPECT_LT(emitter.particles[0].velocity.x, 0.f);
}

TEST(ParticleSdf, KillAndLifetimeLossAreShared) {
    const engine::SdfField2D field = ground_field();
    const std::vector<ParticleCollider2D> colliders{sdf_collider(field)};

    ParticleEmitter kill = collision_emitter();
    kill.kill_on_collision = true;
    kill.particles.push_back(particle_at({0.f, 0.6f}, {0.f, -12.f}));
    kill.particles.push_back(particle_at({3.f, 3.f}, {0.f, 0.f}));
    step(kill, colliders);
    ASSERT_EQ(kill.active_count(), 1u);
    EXPECT_FLOAT_EQ(kill.particles[0].position.x, 3.f);

    ParticleEmitter loss = collision_emitter();
    loss.lifetime_loss = 0.25f;
    loss.particles.push_back(particle_at({0.f, 0.6f}, {0.f, -12.f}));
    loss.particles[0].lifetime = 1.0f;
    step(loss, colliders);
    ASSERT_EQ(loss.active_count(), 1u);
    EXPECT_NEAR(loss.particles[0].age, kDt + 0.25f, 1e-5f);
}

TEST(ParticleSdf, EmptyAndUnallocatedFieldsBehaveAsDocumented) {
    // Empty outside and nothing allocated: no contact anywhere.
    const engine::SdfField2D empty{engine::SdfField2DDesc{.cell_size = kCell, .band = kBand}};
    // Solid outside and nothing allocated: flat at -band, no normal, so no contact either.
    const engine::SdfField2D solid{
            engine::SdfField2DDesc{.cell_size = kCell, .band = kBand, .outside = engine::SdfOutside::Solid}};
    for (const engine::SdfField2D* field : {&empty, &solid}) {
        const std::vector<ParticleCollider2D> colliders{sdf_collider(*field)};
        ParticleEmitter emitter = collision_emitter();
        emitter.particles.push_back(particle_at({0.f, 0.f}, {0.f, -6.f}));
        step(emitter, colliders, 10);
        EXPECT_NEAR(emitter.particles[0].position.y, -1.f, 1e-4f);
        EXPECT_FLOAT_EQ(emitter.particles[0].velocity.y, -6.f);
    }

    // Solid outside with a cavity: a particle deep in the unallocated solid moves freely, one in the cavity is held.
    const engine::SdfField2D cavity = cavity_field(2.f);
    const std::vector<ParticleCollider2D> colliders{sdf_collider(cavity)};
    ParticleEmitter emitter = collision_emitter();
    emitter.particles.push_back(particle_at({40.f, 0.f}, {0.f, -6.f}));
    emitter.particles.push_back(particle_at({0.f, 0.f}, {0.f, -6.f}));
    step(emitter, colliders, 60);
    EXPECT_NEAR(emitter.particles[0].position.y, -6.f, 1e-3f);
    EXPECT_GE(cavity.sample(glm::vec2(emitter.particles[1].position)), 0.f);
    EXPECT_GT(emitter.particles[1].position.y, -2.f);
}

TEST(ParticleSdf, SameResultWithAndWithoutWorkers) {
    const engine::SdfField2D field = cavity_field();
    const engine::SdfField2D floor = ground_field();
    const std::vector<ParticleCollider2D> colliders{sdf_collider(field), sdf_collider(floor, 1, {0.f, -1.f}, 0.3f)};
    ParticleEmitter seeded = collision_emitter({0.f, -10.f, 0.f});
    seeded.collision_radius = 0.05f;
    fill_cavity(seeded, 30);
    ASSERT_GT(seeded.active_count(), 4 * 64u);

    ParticleEmitter serial = seeded;
    ParticleEmitter inline_jobs = seeded;
    ParticleEmitter pooled = seeded;
    engine::JobSystem none(0);
    engine::JobSystem jobs(3);
    step(serial, colliders, 60);
    step(inline_jobs, colliders, 60, &none);
    step(pooled, colliders, 60, &jobs);

    ASSERT_EQ(serial.active_count(), inline_jobs.active_count());
    ASSERT_EQ(serial.active_count(), pooled.active_count());
    for (std::size_t i = 0; i < serial.active_count(); ++i) {
        EXPECT_EQ(serial.particles[i].position, inline_jobs.particles[i].position) << i;
        EXPECT_EQ(serial.particles[i].velocity, inline_jobs.particles[i].velocity) << i;
        EXPECT_EQ(serial.particles[i].position, pooled.particles[i].position) << i;
        EXPECT_EQ(serial.particles[i].velocity, pooled.particles[i].velocity) << i;
    }
}

TEST(ParticleSdf, LocalSimulationSpaceBouncesInWorldSpace) {
    const engine::SdfField2D field = ground_field();
    const std::vector<ParticleCollider2D> colliders{sdf_collider(field)};
    ParticleEmitter emitter = collision_emitter();
    emitter.simulation_space = engine::render::SimulationSpace::Local;
    // Local y = 0 is world y = 2.
    const glm::mat4 transform = glm::translate(glm::mat4{1.0f}, glm::vec3{0.f, 2.f, 0.f});
    emitter.particles.push_back(particle_at({0.f, 0.f}, {0.f, -6.f}));
    step(emitter, colliders, 30, nullptr, transform);
    EXPECT_GE(emitter.particles[0].position.y + 2.f, kGround);
    EXPECT_GT(emitter.particles[0].velocity.y, 0.f);
}

TEST(ParticleSdfEcs, RunParticlesCollidesWithSdfTerrain) {
    engine::ecs::World world;
    add_terrain(world, ground_field());
    const engine::ecs::Entity emitter = add_emitter(world, particle_at({0.f, 2.f}, {0.f, -6.f}));
    run_frames(world, 60);
    EXPECT_GE(only_particle(world, emitter).position.y, kGround);
}

TEST(ParticleSdfEcs, LayerAndMaskFilter) {
    engine::ecs::World world;
    add_terrain(world, ground_field(), 2);
    const engine::ecs::Entity missed = add_emitter(world, particle_at({0.f, 2.f}, {0.f, -6.f}), 1);
    const engine::ecs::Entity hit = add_emitter(world, particle_at({0.f, 2.f}, {0.f, -6.f}), 2);
    run_frames(world, 60);
    EXPECT_LT(only_particle(world, missed).position.y, kGround - 1.f);
    EXPECT_GE(only_particle(world, hit).position.y, kGround);
}

TEST(ParticleSdfEcs, RotatedTerrainIsAWall) {
    engine::ecs::World world;
    // Turned a quarter turn: local +Y (out of the ground) points along world -X, and the surface is x = -kGround.
    add_terrain(world, ground_field(), 1,
                engine::Transform{.position = {0.f, 0.f, 0.f},
                                  .rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3{0.f, 0.f, 1.f})});
    const engine::ecs::Entity emitter = add_emitter(world, particle_at({-3.f, 0.f}, {6.f, 0.f}));
    run_frames(world, 60);
    const Particle& p = only_particle(world, emitter);
    EXPECT_LE(p.position.x, -kGround);
    EXPECT_LT(p.velocity.x, 0.f);
    EXPECT_NEAR(p.velocity.y, 0.f, 1e-3f);
}

TEST(ParticleSdfEcs, TerrainUnderADynamicBodyIsIgnored) {
    engine::ecs::World world;
    const engine::ecs::Entity dynamic = add_terrain(world, ground_field());
    world.emplace<engine::RigidBody2D>(dynamic, engine::RigidBody2D{.type = engine::BodyType::Dynamic});
    // A child of a Dynamic body follows it, so it is ignored too.
    const engine::ecs::Entity body = world.create();
    world.emplace<engine::Transform>(body, engine::Transform{.position = {10.f, 0.f, 0.f}});
    world.emplace<engine::RigidBody2D>(body, engine::RigidBody2D{.type = engine::BodyType::Dynamic});
    const engine::ecs::Entity child = add_terrain(world, ground_field());
    engine::ecs::set_parent(world, child, body);
    // Kinematic terrain still collides.
    const engine::ecs::Entity kinematic =
            add_terrain(world, ground_field(), 1, engine::Transform{.position = {30.f, 0.f, 0.f}});
    world.emplace<engine::RigidBody2D>(kinematic, engine::RigidBody2D{.type = engine::BodyType::Kinematic});

    const engine::ecs::Entity through = add_emitter(world, particle_at({0.f, 2.f}, {0.f, -6.f}));
    const engine::ecs::Entity through_child = add_emitter(world, particle_at({10.f, 2.f}, {0.f, -6.f}));
    const engine::ecs::Entity held = add_emitter(world, particle_at({30.f, 2.f}, {0.f, -6.f}));
    run_frames(world, 60);
    EXPECT_LT(only_particle(world, through).position.y, kGround - 1.f);
    EXPECT_LT(only_particle(world, through_child).position.y, kGround - 1.f);
    EXPECT_GE(only_particle(world, held).position.y, kGround);
}
