#include <gtest/gtest.h>

#include <engine/core/job_system.h>
#include <engine/core/time.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/physics_3d.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/render/command_buffer.h>
#include <engine/render/particles.h>

#include "render/particle_shape.h"
#include "render/shader_instance_inputs.h"
#include <engine/render/primitives.h>
#include <engine/render/sprite.h>
#include <engine/ui/canvas.h>
#include <engine/core/bound_windows.h>
#include <engine/ui/presentation.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace {

struct OneCell {
    glm::ivec3 cell{0};
};

bool one_cell_solid(const void* data, glm::ivec3 cell) {
    const auto* solid = static_cast<const OneCell*>(data);
    return cell == solid->cell;
}

class FakeMesh final : public engine::render::IMesh {
public:
    FakeMesh() : engine::render::IMesh(engine::render::make_quad()) {}
    explicit FakeMesh(const engine::render::MeshDesc& desc) : engine::render::IMesh(desc) {}

protected:
    bool replace(const engine::render::MeshDesc&) override {
        return true;
    }
};

class FakeMaterial final : public engine::render::IMaterial {
public:
    explicit FakeMaterial(engine::render::BlendMode blend = engine::render::BlendMode::Alpha)
        : blend_(blend) {}

    std::shared_ptr<engine::render::IShader> shader() const override {
        return {};
    }

    std::shared_ptr<engine::render::ITexture> texture(int) const override {
        return {};
    }

    glm::vec4 color() const override {
        return {1.0f, 1.0f, 1.0f, 1.0f};
    }

    engine::render::RenderState render_state() const override {
        return {.blend = blend_};
    }

private:
    engine::render::BlendMode blend_;
};

void spawn_camera(engine::ecs::World& world) {
    const engine::ecs::Entity camera = world.create();
    world.emplace<engine::Transform>(camera, engine::Transform{.position = {0.0f, 0.0f, 10.0f}});
    // From z = 10 the clip range has to reach the sprites on the z = 0 plane; the default -1..1 does not.
    world.emplace<engine::Camera>(camera, engine::Camera{.ortho_size = 5.0f, .near_clip = -100.0f, .far_clip = 100.0f});
    world.ctx<engine::ActiveCamera>().entity = camera;
    engine::ui::presentation_of(world).sizes.sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    world.ctx<engine::BoundWindows>().ids = {engine::kPrimaryWindow};
}

}

TEST(ParticleEmitter, DefaultsMatchExpected) {
    engine::render::ParticleEmitter emitter;
    EXPECT_TRUE(emitter.playing);
    EXPECT_TRUE(emitter.looping);
    EXPECT_EQ(emitter.active_count(), 0u);
    EXPECT_EQ(emitter.max_particles, 1000u);
    EXPECT_EQ(emitter.emission_rate, 10.0f);
    EXPECT_EQ(emitter.simulation_space, engine::render::SimulationSpace::World);
    EXPECT_EQ(emitter.draw, engine::render::ParticleDraw::Plane);
    EXPECT_TRUE(emitter.frustum_cull);
    EXPECT_EQ(emitter.mesh_scale, (glm::vec3{1.0f, 1.0f, 1.0f}));
    EXPECT_FLOAT_EQ(emitter.texture_layer, 0.0f);
    EXPECT_FALSE(engine::render::particle_draw_uses_instance_transform(engine::render::ParticleDraw::Plane));
    EXPECT_FALSE(engine::render::particle_draw_uses_instance_transform(engine::render::ParticleDraw::Billboard));
    EXPECT_FALSE(engine::render::particle_draw_uses_instance_transform(engine::render::ParticleDraw::Stretched));
    EXPECT_TRUE(engine::render::particle_draw_uses_instance_transform(engine::render::ParticleDraw::Mesh));
}

TEST(ParticleEmitter, ContinuousEmissionSpawnsAtRate) {
    engine::render::ParticleEmitter emitter;
    emitter.emission_rate = 10.0f; // 10 particles/sec
    emitter.lifetime_min = 5.0f;
    emitter.lifetime_max = 5.0f;

    // Advance 0.5s -> should spawn 5 particles
    engine::render::update_emitter(emitter, 0.5f);
    EXPECT_EQ(emitter.active_count(), 5u);

    // Advance another 0.5s -> should spawn 5 more particles
    engine::render::update_emitter(emitter, 0.5f);
    EXPECT_EQ(emitter.active_count(), 10u);
}

TEST(ParticleEmitter, BurstSpawnsImmediatelyAndRespectsMax) {
    engine::render::ParticleEmitter emitter;
    emitter.emission_rate = 0.0f; // no continuous emission
    emitter.max_particles = 25;
    emitter.lifetime_min = 10.0f;
    emitter.lifetime_max = 10.0f;

    emitter.burst(15);
    engine::render::update_emitter(emitter, 0.016f);
    EXPECT_EQ(emitter.active_count(), 15u);

    // Request 20 more, but capacity only has 10 left (25 total)
    emitter.burst(20);
    engine::render::update_emitter(emitter, 0.016f);
    EXPECT_EQ(emitter.active_count(), 25u);
}

TEST(ParticleEmitter, ParticlesDieAfterLifetime) {
    engine::render::ParticleEmitter emitter;
    emitter.emission_rate = 0.0f;
    emitter.lifetime_min = 1.0f;
    emitter.lifetime_max = 1.0f;

    emitter.burst(10);
    engine::render::update_emitter(emitter, 0.1f);
    EXPECT_EQ(emitter.active_count(), 10u);

    // Advance to 0.9s -> still alive
    engine::render::update_emitter(emitter, 0.8f);
    EXPECT_EQ(emitter.active_count(), 10u);

    // Advance past 1.0s -> all should expire
    engine::render::update_emitter(emitter, 0.2f);
    EXPECT_EQ(emitter.active_count(), 0u);
}

TEST(ParticleEmitter, StopClearsParticlesAndResets) {
    engine::render::ParticleEmitter emitter;
    emitter.emission_rate = 0.0f;
    emitter.burst(10);
    engine::render::update_emitter(emitter, 0.1f);
    EXPECT_EQ(emitter.active_count(), 10u);

    emitter.stop();
    EXPECT_FALSE(emitter.playing);
    EXPECT_EQ(emitter.active_count(), 0u);

    // Further updates do nothing while stopped
    engine::render::update_emitter(emitter, 0.5f);
    EXPECT_EQ(emitter.active_count(), 0u);
}

TEST(ParticleEmitter, GravityAndVelocityIntegration) {
    engine::render::ParticleEmitter emitter;
    emitter.emission_rate = 0.0f;
    emitter.lifetime_min = 2.0f;
    emitter.lifetime_max = 2.0f;
    emitter.speed_min = 10.0f;
    emitter.speed_max = 10.0f;
    emitter.direction = {1.0f, 0.0f, 0.0f};
    emitter.spread_angle = 0.0f;
    emitter.gravity = {0.0f, -9.8f, 0.0f};

    emitter.burst(1);
    engine::render::update_emitter(emitter, 0.001f);
    ASSERT_EQ(emitter.active_count(), 1u);

    const glm::vec3 pos_start = emitter.particles[0].position;

    // Advance 1 second
    engine::render::update_emitter(emitter, 1.0f);
    ASSERT_EQ(emitter.active_count(), 1u);

    EXPECT_GT(emitter.particles[0].position.x, pos_start.x);
    EXPECT_LT(emitter.particles[0].position.y, pos_start.y);
}

TEST(ParticleEmitter, ColorAndSizeInterpolation) {
    engine::render::ParticleEmitter emitter;
    emitter.emission_rate = 0.0f;
    emitter.lifetime_min = 2.0f;
    emitter.lifetime_max = 2.0f;
    emitter.color_start = {1.0f, 0.0f, 0.0f, 1.0f};
    emitter.color_end = {0.0f, 0.0f, 1.0f, 0.0f};
    emitter.size_start_min = {2.0f, 2.0f};
    emitter.size_start_max = {2.0f, 2.0f};
    emitter.size_end_min = {0.0f, 0.0f};
    emitter.size_end_max = {0.0f, 0.0f};

    emitter.burst(1);
    engine::render::update_emitter(emitter, 0.001f);
    ASSERT_EQ(emitter.active_count(), 1u);

    // Initial values
    EXPECT_NEAR(emitter.particles[0].color.r, 1.0f, 0.01f);
    EXPECT_NEAR(emitter.particles[0].color.b, 0.0f, 0.01f);
    EXPECT_NEAR(emitter.particles[0].size.x, 2.0f, 0.01f);

    // Advance to 50% lifetime (1.0s)
    engine::render::update_emitter(emitter, 0.999f);
    ASSERT_EQ(emitter.active_count(), 1u);
    EXPECT_NEAR(emitter.particles[0].color.r, 0.5f, 0.05f);
    EXPECT_NEAR(emitter.particles[0].color.b, 0.5f, 0.05f);
    EXPECT_NEAR(emitter.particles[0].size.x, 1.0f, 0.05f);
}

TEST(ParticleEmitter, WorldSpaceParticlesStayIndependentOfEmitterMovement) {
    engine::render::ParticleEmitter emitter;
    emitter.emission_rate = 0.0f;
    emitter.simulation_space = engine::render::SimulationSpace::World;
    emitter.lifetime_min = 5.0f;
    emitter.lifetime_max = 5.0f;
    emitter.speed_min = 0.0f;
    emitter.speed_max = 0.0f;

    glm::mat4 transform1 = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f));
    emitter.burst(1);
    engine::render::update_emitter(emitter, 0.016f, transform1);
    ASSERT_EQ(emitter.active_count(), 1u);
    EXPECT_FLOAT_EQ(emitter.particles[0].position.x, 10.0f);

    // Move transform to x = 50.0f
    glm::mat4 transform2 = glm::translate(glm::mat4(1.0f), glm::vec3(50.0f, 0.0f, 0.0f));
    engine::render::update_emitter(emitter, 0.016f, transform2);
    // In world space, existing particle remains at x = 10.0f
    EXPECT_FLOAT_EQ(emitter.particles[0].position.x, 10.0f);
}

TEST(ParticleSystemEcs, RunParticlesUpdatesAllEmittersInWorld) {
    engine::ecs::World world;
    world.ctx<engine::Time>().delta_time = 0.5f;

    const engine::ecs::Entity e1 = world.create();
    world.emplace<engine::render::ParticleEmitter>(e1, engine::render::ParticleEmitter{
            .emission_rate = 10.0f,
            .lifetime_min = 2.0f,
            .lifetime_max = 2.0f,
    });

    const engine::ecs::Entity e2 = world.create();
    world.emplace<engine::render::ParticleEmitter>(e2, engine::render::ParticleEmitter{
            .emission_rate = 20.0f,
            .lifetime_min = 2.0f,
            .lifetime_max = 2.0f,
    });

    engine::run_particles(world);

    EXPECT_EQ(world.get<engine::render::ParticleEmitter>(e1).active_count(), 5u);
    EXPECT_EQ(world.get<engine::render::ParticleEmitter>(e2).active_count(), 10u);
}

TEST(ParticleSystemRender, RunRenderPushesCmdDrawParticles) {
    engine::ecs::World world;
    spawn_camera(world);

    engine::render::CommandBuffer commands;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});

    const engine::ecs::Entity entity = world.create();
    auto& em = world.emplace<engine::render::ParticleEmitter>(entity);
    em.mesh = std::make_shared<FakeMesh>();
    em.material = std::make_shared<FakeMaterial>(engine::render::BlendMode::Additive);
    em.layer = 2;
    em.order_in_layer = 5;
    em.burst(3);
    engine::render::update_emitter(em, 0.016f);
    ASSERT_EQ(em.active_count(), 3u);

    world.run(engine::ecs::Schedule::Frame);

    ASSERT_EQ(commands.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdSetView>(commands[0]));
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawParticles>(commands[1]));

    const auto& cmd = std::get<engine::render::CmdDrawParticles>(commands[1]);
    EXPECT_EQ(cmd.mesh, em.mesh);
    EXPECT_EQ(cmd.material, em.material);
    EXPECT_EQ(cmd.blend, engine::render::BlendMode::Additive);
    EXPECT_EQ(cmd.draw, engine::render::ParticleDraw::Plane);
    EXPECT_EQ(cmd.instances.size(), 3u);
}

TEST(ParticleSystemRender, ParticleEmitterSortsBetweenSpritesByLayer) {
    engine::ecs::World world;
    spawn_camera(world);

    engine::render::CommandBuffer commands;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});

    auto mesh = std::make_shared<FakeMesh>();
    auto mat = std::make_shared<FakeMaterial>();

    // Background sprite at layer 0
    const engine::ecs::Entity bg_entity = world.create();
    world.emplace<engine::Transform>(bg_entity);
    world.emplace<engine::render::Sprite>(bg_entity, engine::render::Sprite{
            .layer = 0,
            .mesh = mesh,
            .material = mat,
    });

    // Particle emitter at layer 1
    const engine::ecs::Entity part_entity = world.create();
    auto& em = world.emplace<engine::render::ParticleEmitter>(part_entity);
    em.layer = 1;
    em.mesh = mesh;
    em.material = mat;
    em.burst(2);
    engine::render::update_emitter(em, 0.016f);

    // Foreground sprite at layer 2
    const engine::ecs::Entity fg_entity = world.create();
    world.emplace<engine::Transform>(fg_entity);
    world.emplace<engine::render::Sprite>(fg_entity, engine::render::Sprite{
            .layer = 2,
            .mesh = mesh,
            .material = mat,
    });

    world.run(engine::ecs::Schedule::Frame);

    ASSERT_EQ(commands.size(), 4u);
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdSetView>(commands[0]));
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(commands[1]));
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawParticles>(commands[2]));
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(commands[3]));
}

TEST(Curve, EvaluateLinearClampsExtremes) {
    engine::render::Curve<float> curve{{
            {0.0f, 10.0f},
            {1.0f, 20.0f},
    }};

    EXPECT_FLOAT_EQ(curve.evaluate(-1.0f), 10.0f);
    EXPECT_FLOAT_EQ(curve.evaluate(0.0f), 10.0f);
    EXPECT_NEAR(curve.evaluate(0.5f), 15.0f, 0.001f);
    EXPECT_FLOAT_EQ(curve.evaluate(1.0f), 20.0f);
    EXPECT_FLOAT_EQ(curve.evaluate(2.0f), 20.0f);
}

TEST(Curve, EvaluateMultiPointSmoothstep) {
    engine::render::Curve<float> curve{{
            {0.0f, 0.0f, engine::render::KeyInterpolation::Smooth},
            {0.5f, 10.0f, engine::render::KeyInterpolation::Smooth},
            {1.0f, 0.0f, engine::render::KeyInterpolation::Linear},
    }};

    EXPECT_FLOAT_EQ(curve.evaluate(0.0f), 0.0f);
    EXPECT_NEAR(curve.evaluate(0.5f), 10.0f, 0.001f);
    EXPECT_FLOAT_EQ(curve.evaluate(1.0f), 0.0f);
    // Smoothstep at quarter-way: s = 0.5 * 0.5 * (3 - 1) = 0.5 -> 5.0f
    EXPECT_NEAR(curve.evaluate(0.25f), 5.0f, 0.01f);
}

TEST(Curve, EvaluateStepInterpolation) {
    engine::render::Curve<float> curve{{
            {0.0f, 5.0f, engine::render::KeyInterpolation::Step},
            {0.5f, 15.0f, engine::render::KeyInterpolation::Step},
            {1.0f, 25.0f, engine::render::KeyInterpolation::Step},
    }};

    EXPECT_FLOAT_EQ(curve.evaluate(0.2f), 5.0f);
    EXPECT_FLOAT_EQ(curve.evaluate(0.5f), 15.0f);
    EXPECT_FLOAT_EQ(curve.evaluate(0.8f), 15.0f);
    EXPECT_FLOAT_EQ(curve.evaluate(1.0f), 25.0f);
}

TEST(ParticleEmitter, SizeCurveSwellsAndShrinksNonLinearly) {
    engine::render::ParticleEmitter emitter;
    emitter.emission_rate = 0.0f;
    emitter.lifetime_min = 2.0f;
    emitter.lifetime_max = 2.0f;
    emitter.size_start_min = {10.0f, 10.0f};
    emitter.size_start_max = {10.0f, 10.0f};

    // Curve: starts at 0 scale, peaks at 2.0x scale at t = 0.5, shrinks to 0.1x at t = 1.0
    emitter.size_curve = engine::render::Curve<float>{{
            {0.0f, 0.0f, engine::render::KeyInterpolation::Linear},
            {0.5f, 2.0f, engine::render::KeyInterpolation::Linear},
            {1.0f, 0.1f, engine::render::KeyInterpolation::Linear},
    }};

    emitter.burst(1);
    engine::render::update_emitter(emitter, 0.0001f);
    ASSERT_EQ(emitter.active_count(), 1u);

    // Initial size should be ~0
    EXPECT_NEAR(emitter.particles[0].size.x, 0.0f, 0.01f);

    // Advance to 50% lifetime (1.0s elapsed)
    engine::render::update_emitter(emitter, 1.0f);
    ASSERT_EQ(emitter.active_count(), 1u);
    // At t = 0.5, size should be 10 * 2.0 = 20.0f (swelled)
    EXPECT_NEAR(emitter.particles[0].size.x, 20.0f, 0.1f);

    // Advance to near end of lifetime (total 1.95s elapsed out of 2.0s -> t = ~0.975)
    engine::render::update_emitter(emitter, 0.95f);
    ASSERT_EQ(emitter.active_count(), 1u);
    // Size should now have shrunk back down (near 10 * 0.1 = 1.0f)
    EXPECT_LT(emitter.particles[0].size.x, 2.0f);
    EXPECT_GT(emitter.particles[0].size.x, 0.5f);
}

TEST(ParticleEmitter, ColorAndAlphaCurvesAnimateCustomGradients) {
    engine::render::ParticleEmitter emitter;
    emitter.emission_rate = 0.0f;
    emitter.lifetime_min = 4.0f;
    emitter.lifetime_max = 4.0f;

    // Multi-stop color curve
    emitter.color_curve = engine::render::Curve<glm::vec4>{{
            {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}}, // Red
            {0.5f, {0.0f, 1.0f, 0.0f, 1.0f}}, // Green
            {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}}, // Blue
    }};

    // Custom alpha curve that fades in, holds, and fades out
    emitter.alpha_curve = engine::render::Curve<float>{{
            {0.0f, 0.0f},
            {0.25f, 1.0f},
            {0.75f, 1.0f},
            {1.0f, 0.0f},
    }};

    emitter.burst(1);
    engine::render::update_emitter(emitter, 0.0001f);
    ASSERT_EQ(emitter.active_count(), 1u);

    // At spawn: Red color, transparent alpha
    EXPECT_NEAR(emitter.particles[0].color.r, 1.0f, 0.01f);
    EXPECT_NEAR(emitter.particles[0].color.a, 0.0f, 0.01f);

    // Advance to 1.0s (t = 0.25): alpha should be fully opaque (1.0)
    engine::render::update_emitter(emitter, 1.0f);
    ASSERT_EQ(emitter.active_count(), 1u);
    EXPECT_NEAR(emitter.particles[0].color.a, 1.0f, 0.01f);

    // Advance to 2.0s total (t = 0.5): Green color
    engine::render::update_emitter(emitter, 1.0f);
    ASSERT_EQ(emitter.active_count(), 1u);
    EXPECT_NEAR(emitter.particles[0].color.g, 1.0f, 0.05f);
    EXPECT_NEAR(emitter.particles[0].color.r, 0.0f, 0.05f);

    // Advance to 4.0s total (t ~ 1.0): Blue color, 0 alpha
    engine::render::update_emitter(emitter, 1.99f);
    ASSERT_EQ(emitter.active_count(), 1u);
    EXPECT_NEAR(emitter.particles[0].color.b, 1.0f, 0.05f);
    EXPECT_NEAR(emitter.particles[0].color.a, 0.0f, 0.05f);
}

TEST(ParticleCollision, ParticlesBounceOffBoxFloor) {
    engine::render::ParticleEmitter emitter;
    emitter.emission_rate = 0.0f;
    emitter.lifetime_min = 5.0f;
    emitter.lifetime_max = 5.0f;
    emitter.speed_min = 0.0f;
    emitter.speed_max = 0.0f;
    emitter.gravity = {0.0f, -10.0f, 0.0f};
    emitter.collision_enabled = true;
    emitter.bounce = 0.6f;
    emitter.friction = 0.0f;

    // Floor at y = 0, height = 2 (so top of box is at y = 1.0f)
    std::vector<engine::render::ParticleCollider2D> colliders{
            engine::render::ParticleCollider2D{
                    .shape = engine::render::ParticleCollider2D::Shape::Box,
                    .position = {0.0f, 0.0f, 0.0f},
                    .box_size = {100.0f, 2.0f, 1.0f},
                    .circle_radius = 0.0f,
                    .layer = 1,
            },
    };

    emitter.burst(1);
    // Spawn at (0, 5, 0)
    glm::mat4 transform = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 5.0f, 0.0f});
    engine::render::update_emitter(emitter, 0.0001f, transform, colliders);
    ASSERT_EQ(emitter.active_count(), 1u);
    EXPECT_NEAR(emitter.particles[0].position.y, 5.0f, 0.01f);

    // Advance 0.5s: falls with gravity
    engine::render::update_emitter(emitter, 0.5f, transform, colliders);
    ASSERT_EQ(emitter.active_count(), 1u);
    // At t=0.5, falls to around y = 5 - 0.5 * 10 * 0.5^2 = 3.75, still above floor
    EXPECT_GT(emitter.particles[0].position.y, 1.0f);
    EXPECT_LT(emitter.particles[0].velocity.y, 0.0f);

    // Advance 0.6s more (total > 1s): hits floor at y = 1.0f and bounces up!
    engine::render::update_emitter(emitter, 0.6f, transform, colliders);
    ASSERT_EQ(emitter.active_count(), 1u);
    // Particle should have bounced upwards!
    EXPECT_GE(emitter.particles[0].position.y, 1.0f);
    EXPECT_GT(emitter.particles[0].velocity.y, 0.0f);
}

TEST(ParticleCollision, ParticlesBounceOffCircleObstacle) {
    engine::render::ParticleEmitter emitter;
    emitter.emission_rate = 0.0f;
    emitter.lifetime_min = 5.0f;
    emitter.lifetime_max = 5.0f;
    emitter.speed_min = 10.0f;
    emitter.speed_max = 10.0f;
    emitter.direction = {1.0f, 0.0f, 0.0f}; // moving right
    emitter.gravity = {0.0f, 0.0f, 0.0f};
    emitter.collision_enabled = true;
    emitter.bounce = 0.8f;

    // Circle obstacle at x = 5.0, radius = 2.0 (left edge is at x = 3.0)
    std::vector<engine::render::ParticleCollider2D> colliders{
            engine::render::ParticleCollider2D{
                    .shape = engine::render::ParticleCollider2D::Shape::Circle,
                    .position = {5.0f, 0.0f, 0.0f},
                    .box_size = {0.0f, 0.0f, 0.0f},
                    .circle_radius = 2.0f,
                    .layer = 1,
            },
    };

    emitter.burst(1);
    engine::render::update_emitter(emitter, 0.0001f, glm::mat4{1.0f}, colliders);
    ASSERT_EQ(emitter.active_count(), 1u);

    // Initial position is at 0, moving right at +10
    EXPECT_NEAR(emitter.particles[0].position.x, 0.0f, 0.01f);
    EXPECT_GT(emitter.particles[0].velocity.x, 0.0f);

    // Advance 0.5s: moves 5 units, hits circle obstacle at x = 3.0, bounces left!
    engine::render::update_emitter(emitter, 0.5f, glm::mat4{1.0f}, colliders);
    ASSERT_EQ(emitter.active_count(), 1u);
    // Bounced: velocity.x should now be negative!
    EXPECT_LT(emitter.particles[0].velocity.x, 0.0f);
    EXPECT_LE(emitter.particles[0].position.x, 3.1f);
}

TEST(ParticleCollision, KillOnCollisionTerminatesParticle) {
    engine::render::ParticleEmitter emitter;
    emitter.emission_rate = 0.0f;
    emitter.lifetime_min = 10.0f;
    emitter.lifetime_max = 10.0f;
    emitter.speed_min = 10.0f;
    emitter.speed_max = 10.0f;
    emitter.direction = {0.0f, -1.0f, 0.0f}; // moving down
    emitter.gravity = {0.0f, 0.0f, 0.0f};
    emitter.collision_enabled = true;
    emitter.kill_on_collision = true;

    // Floor at y = -2
    std::vector<engine::render::ParticleCollider2D> colliders{
            engine::render::ParticleCollider2D{
                    .shape = engine::render::ParticleCollider2D::Shape::Box,
                    .position = {0.0f, -2.0f, 0.0f},
                    .box_size = {50.0f, 1.0f, 1.0f},
                    .circle_radius = 0.0f,
                    .layer = 1,
            },
    };

    emitter.burst(1);
    engine::render::update_emitter(emitter, 0.0001f, glm::mat4{1.0f}, colliders);
    ASSERT_EQ(emitter.active_count(), 1u);

    // Step 0.3s (moves 3 units down, passes y = -1.5, hits floor)
    engine::render::update_emitter(emitter, 0.3f, glm::mat4{1.0f}, colliders);
    // Particle must be killed!
    EXPECT_EQ(emitter.active_count(), 0u);
}

TEST(ParticleCollision, FrictionDampsTangentialVelocity) {
    engine::render::ParticleEmitter emitter;
    emitter.emission_rate = 0.0f;
    emitter.lifetime_min = 5.0f;
    emitter.lifetime_max = 5.0f;
    emitter.speed_min = 0.0f;
    emitter.speed_max = 0.0f;
    emitter.collision_enabled = true;
    emitter.bounce = 0.5f;
    emitter.friction = 0.75f; // 75% friction damping

    // Floor at y = 0, top edge at y = 0.5
    std::vector<engine::render::ParticleCollider2D> colliders{
            engine::render::ParticleCollider2D{
                    .shape = engine::render::ParticleCollider2D::Shape::Box,
                    .position = {0.0f, 0.0f, 0.0f},
                    .box_size = {100.0f, 1.0f, 1.0f},
                    .circle_radius = 0.0f,
                    .layer = 1,
            },
    };

    emitter.burst(1);
    engine::render::update_emitter(emitter, 0.0001f, glm::mat4{1.0f}, colliders);
    ASSERT_EQ(emitter.active_count(), 1u);

    // Manually give particle an angled velocity: vx = 10, vy = -10 at position (0, 2, 0)
    emitter.particles[0].position = {0.0f, 2.0f, 0.0f};
    emitter.particles[0].velocity = {10.0f, -10.0f, 0.0f};

    // Step 0.3s (falls into floor at y = 0.5)
    engine::render::update_emitter(emitter, 0.3f, glm::mat4{1.0f}, colliders);
    ASSERT_EQ(emitter.active_count(), 1u);

    // vy bounced: should be positive (+5)
    EXPECT_GT(emitter.particles[0].velocity.y, 0.0f);
    // vx damped by 75%: 10 * (1 - 0.75) = 2.5
    EXPECT_NEAR(emitter.particles[0].velocity.x, 2.5f, 0.1f);
}

TEST(ParticleCollision, CollisionMaskFiltersLayers) {
    engine::render::ParticleEmitter emitter;
    emitter.emission_rate = 0.0f;
    emitter.lifetime_min = 5.0f;
    emitter.lifetime_max = 5.0f;
    emitter.speed_min = 10.0f;
    emitter.speed_max = 10.0f;
    emitter.direction = {0.0f, -1.0f, 0.0f};
    emitter.gravity = {0.0f, 0.0f, 0.0f};
    emitter.collision_enabled = true;
    emitter.collision_mask = 0b0001; // only collide with layer 1
    emitter.kill_on_collision = true;

    // Collider on layer 2 (0b0010)
    std::vector<engine::render::ParticleCollider2D> colliders{
            engine::render::ParticleCollider2D{
                    .shape = engine::render::ParticleCollider2D::Shape::Box,
                    .position = {0.0f, -1.0f, 0.0f},
                    .box_size = {10.0f, 1.0f, 1.0f},
                    .circle_radius = 0.0f,
                    .layer = 0b0010,
            },
    };

    emitter.burst(1);
    engine::render::update_emitter(emitter, 0.0001f, glm::mat4{1.0f}, colliders);
    ASSERT_EQ(emitter.active_count(), 1u);

    // Step 0.3s: moves through layer 2 collider without colliding
    engine::render::update_emitter(emitter, 0.3f, glm::mat4{1.0f}, colliders);
    // Still alive because layer didn't match mask!
    EXPECT_EQ(emitter.active_count(), 1u);
    EXPECT_LT(emitter.particles[0].position.y, -1.5f);
}

engine::render::ParticleEmitter collision_emitter(float bounce) {
    engine::render::ParticleEmitter emitter;
    emitter.emission_rate = 0.0f;
    emitter.lifetime_min = 5.0f;
    emitter.lifetime_max = 5.0f;
    emitter.speed_min = 0.0f;
    emitter.speed_max = 0.0f;
    emitter.gravity = {0.0f, 0.0f, 0.0f};
    emitter.collision_enabled = true;
    emitter.bounce = bounce;
    emitter.friction = 0.0f;
    emitter.burst(1);
    return emitter;
}

void spawn_at(
        engine::render::ParticleEmitter& emitter, glm::vec3 position, glm::vec3 velocity,
        std::span<const engine::render::ParticleCollider2D> colliders = {},
        std::span<const engine::render::ParticleCollider3D> colliders_3d = {}) {
    engine::render::update_emitter(emitter, 0.0001f, glm::mat4{1.0f}, colliders, colliders_3d);
    emitter.particles[0].position = position;
    emitter.particles[0].velocity = velocity;
}

TEST(ParticleCollision, Box3DHitsOnZAndBox2DIgnoresZ) {
    const std::vector<engine::render::ParticleCollider3D> boxes_3d{
            engine::render::ParticleCollider3D{
                    .shape = engine::render::ParticleCollider3D::Shape::Box,
                    .position = {0.0f, 0.0f, 0.0f},
                    .rotation = {1.0f, 0.0f, 0.0f, 0.0f},
                    .box_size = {2.0f, 2.0f, 2.0f},
                    .layer = 1,
            },
    };
    engine::render::ParticleEmitter box3d = collision_emitter(1.0f);
    spawn_at(box3d, {0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, -4.0f});
    ASSERT_EQ(box3d.active_count(), 1u);
    engine::render::update_emitter(box3d, 1.0f, glm::mat4{1.0f}, {}, boxes_3d);
    ASSERT_EQ(box3d.active_count(), 1u);
    // t = 0.5 on +Z, then skin 0.001 and velocity * remaining dt.
    EXPECT_NEAR(box3d.particles[0].velocity.z, 4.0f, 0.01f);
    EXPECT_NEAR(box3d.particles[0].position.z, 3.001f, 0.01f);

    const std::vector<engine::render::ParticleCollider2D> boxes_2d{
            engine::render::ParticleCollider2D{
                    .shape = engine::render::ParticleCollider2D::Shape::Box,
                    .position = {0.0f, 0.0f, 0.0f},
                    .box_size = {2.0f, 2.0f, 2.0f},
                    .circle_radius = 0.0f,
                    .layer = 1,
            },
    };
    engine::render::ParticleEmitter box2d = collision_emitter(1.0f);
    spawn_at(box2d, {0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, -4.0f}, boxes_2d);
    ASSERT_EQ(box2d.active_count(), 1u);
    engine::render::update_emitter(box2d, 1.0f, glm::mat4{1.0f}, boxes_2d);
    ASSERT_EQ(box2d.active_count(), 1u);
    // XY is inside, so the 2D wall hits, and Z is not a face: the particle keeps going.
    EXPECT_NEAR(box2d.particles[0].velocity.z, -4.0f, 0.01f);
    EXPECT_NEAR(box2d.particles[0].position.z, -1.0f, 0.02f);
}

TEST(ParticleCollision, SphereBounce) {
    const std::vector<engine::render::ParticleCollider3D> spheres{
            engine::render::ParticleCollider3D{
                    .shape = engine::render::ParticleCollider3D::Shape::Sphere,
                    .position = {0.0f, 0.0f, 5.0f},
                    .radius = 2.0f,
                    .layer = 1,
            },
    };
    engine::render::ParticleEmitter emitter = collision_emitter(0.8f);
    spawn_at(emitter, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 10.0f});
    ASSERT_EQ(emitter.active_count(), 1u);
    engine::render::update_emitter(emitter, 0.5f, glm::mat4{1.0f}, {}, spheres);
    ASSERT_EQ(emitter.active_count(), 1u);
    EXPECT_NEAR(emitter.particles[0].velocity.z, -8.0f, 0.05f);
    EXPECT_NEAR(emitter.particles[0].position.z, 1.399f, 0.02f);
}

TEST(ParticleCollision, CapsuleCapBounce) {
    const std::vector<engine::render::ParticleCollider3D> capsules{
            engine::render::ParticleCollider3D{
                    .shape = engine::render::ParticleCollider3D::Shape::Capsule,
                    .position = {0.0f, 0.0f, 0.0f},
                    .rotation = {1.0f, 0.0f, 0.0f, 0.0f},
                    .radius = 0.5f,
                    .height = 2.0f,
                    .layer = 1,
            },
    };
    engine::render::ParticleEmitter emitter = collision_emitter(1.0f);
    spawn_at(emitter, {0.0f, 4.0f, 0.0f}, {0.0f, -10.0f, 0.0f});
    ASSERT_EQ(emitter.active_count(), 1u);
    engine::render::update_emitter(emitter, 0.5f, glm::mat4{1.0f}, {}, capsules);
    ASSERT_EQ(emitter.active_count(), 1u);
    EXPECT_NEAR(emitter.particles[0].velocity.y, 10.0f, 0.05f);
    EXPECT_NEAR(emitter.particles[0].position.y, 4.001f, 0.02f);
    EXPECT_GT(emitter.particles[0].position.y, 3.0f);
}

TEST(ParticleCollision, GridSweepStopsOnSolidCell) {
    OneCell solid{{0, 0, 0}};
    const engine::render::ParticleGrid grid{&one_cell_solid, &solid};
    engine::render::ParticleEmitter emitter = collision_emitter(0.0f);
    emitter.collision_grid = true;
    spawn_at(emitter, {0.5f, 2.0f, 0.5f}, {0.0f, -4.0f, 0.0f});
    ASSERT_EQ(emitter.active_count(), 1u);
    engine::render::update_emitter(emitter, 0.5f, glm::mat4{1.0f}, {}, {}, nullptr, &grid);
    ASSERT_EQ(emitter.active_count(), 1u);
    EXPECT_NEAR(emitter.particles[0].position.x, 0.5f, 1e-4f);
    EXPECT_NEAR(emitter.particles[0].position.y, 1.0f, 1e-3f);
    EXPECT_NEAR(emitter.particles[0].position.z, 0.5f, 1e-4f);
    EXPECT_NEAR(emitter.particles[0].velocity.y, 0.0f, 1e-4f);
}

TEST(ParticleSystemECS, RunParticlesCollidesWithWorldEntities) {
    engine::ecs::World world;
    world.ctx<engine::Time>().delta_time = 0.5f;

    // Spawn an obstacle entity in the ECS world with BoxCollider2D
    const engine::ecs::Entity floor = world.create();
    world.emplace<engine::Transform>(floor, engine::Transform{.position = {0.0f, 0.0f, 0.0f}});
    world.emplace<engine::BoxCollider2D>(floor, engine::BoxCollider2D{
            .size = {50.0f, 2.0f, 1.0f}, // top edge at y = 1.0
            .layer = 1,
            .is_trigger = false,
    });

    // Spawn an emitter entity with particle collisions enabled
    const engine::ecs::Entity emitter_entity = world.create();
    world.emplace<engine::Transform>(emitter_entity, engine::Transform{.position = {0.0f, 4.0f, 0.0f}});

    engine::render::ParticleEmitter emitter;
    emitter.emission_rate = 0.0f;
    emitter.lifetime_min = 5.0f;
    emitter.lifetime_max = 5.0f;
    emitter.speed_min = 0.0f;
    emitter.speed_max = 0.0f;
    emitter.gravity = {0.0f, -10.0f, 0.0f};
    emitter.collision_enabled = true;
    emitter.bounce = 0.5f;
    emitter.burst(1);

    world.emplace<engine::render::ParticleEmitter>(emitter_entity, std::move(emitter));

    // First frame (dt = 0.001 to spawn particle at emitter transform (0, 4, 0))
    world.ctx<engine::Time>().delta_time = 0.001f;
    engine::run_particles(world);

    auto& em = world.get<engine::render::ParticleEmitter>(emitter_entity);
    ASSERT_EQ(em.active_count(), 1u);
    EXPECT_NEAR(em.particles[0].position.y, 4.0f, 0.05f);

    // Advance 0.8s: falls with gravity towards y=1.0 and bounces
    world.ctx<engine::Time>().delta_time = 0.8f;
    engine::run_particles(world);

    ASSERT_EQ(em.active_count(), 1u);
    // Must have hit the ECS floor collider and bounced upward!
    EXPECT_GE(em.particles[0].position.y, 1.0f);
    EXPECT_GT(em.particles[0].velocity.y, 0.0f);
}

TEST(ParticleSystemECS, OrientedBox3DUsesTransformQuaternion) {
    engine::ecs::World world;
    const engine::ecs::Entity wall = world.create();
    world.emplace<engine::Transform>(
            wall, engine::Transform{
                          .position = {0.0f, 0.0f, 0.0f},
                          .rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f}),
                  });
    world.emplace<engine::BoxCollider3D>(wall, engine::BoxCollider3D{
                                                       .size = {4.0f, 4.0f, 0.2f},
                                                       .layer = 1,
                                                       .is_trigger = false,
                                               });

    const engine::ecs::Entity emitter_entity = world.create();
    world.emplace<engine::Transform>(emitter_entity, engine::Transform{});
    engine::render::ParticleEmitter emitter = collision_emitter(1.0f);
    world.emplace<engine::render::ParticleEmitter>(emitter_entity, std::move(emitter));

    world.ctx<engine::Time>().delta_time = 0.001f;
    engine::run_particles(world);
    auto& em = world.get<engine::render::ParticleEmitter>(emitter_entity);
    ASSERT_EQ(em.active_count(), 1u);
    em.particles[0].position = {3.0f, 0.0f, 0.0f};
    em.particles[0].velocity = {-4.0f, 0.0f, 0.0f};

    world.ctx<engine::Time>().delta_time = 1.0f;
    engine::run_particles(world);
    ASSERT_EQ(em.active_count(), 1u);
    // +90° about Y maps the thin local Z onto world X, so the face is x = 0.1, not the unrotated x = 2.
    EXPECT_NEAR(em.particles[0].velocity.x, 4.0f, 0.05f);
    EXPECT_NEAR(em.particles[0].position.x, 1.201f, 0.02f);
}

TEST(ParticleSystemECS, TriggerBox3DIsNotCopied) {
    engine::ecs::World world;
    const engine::ecs::Entity wall = world.create();
    world.emplace<engine::Transform>(
            wall, engine::Transform{
                          .rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f}),
                  });
    world.emplace<engine::BoxCollider3D>(wall, engine::BoxCollider3D{
                                                       .size = {4.0f, 4.0f, 0.2f},
                                                       .layer = 1,
                                                       .is_trigger = true,
                                               });

    const engine::ecs::Entity emitter_entity = world.create();
    world.emplace<engine::Transform>(emitter_entity, engine::Transform{});
    engine::render::ParticleEmitter emitter = collision_emitter(1.0f);
    emitter.kill_on_collision = true;
    world.emplace<engine::render::ParticleEmitter>(emitter_entity, std::move(emitter));

    world.ctx<engine::Time>().delta_time = 0.001f;
    engine::run_particles(world);
    auto& em = world.get<engine::render::ParticleEmitter>(emitter_entity);
    ASSERT_EQ(em.active_count(), 1u);
    em.particles[0].position = {3.0f, 0.0f, 0.0f};
    em.particles[0].velocity = {-4.0f, 0.0f, 0.0f};

    world.ctx<engine::Time>().delta_time = 1.0f;
    engine::run_particles(world);
    ASSERT_EQ(em.active_count(), 1u);
    EXPECT_LT(em.particles[0].position.x, 0.0f);
}

TEST(ParticleSystemECS, MissingParticleGridDoesNothing) {
    engine::ecs::World world;
    const engine::ecs::Entity emitter_entity = world.create();
    world.emplace<engine::Transform>(emitter_entity, engine::Transform{.position = {0.0f, 0.0f, 2.0f}});
    engine::render::ParticleEmitter emitter = collision_emitter(0.0f);
    emitter.collision_grid = true;
    world.emplace<engine::render::ParticleEmitter>(emitter_entity, std::move(emitter));

    world.ctx<engine::Time>().delta_time = 0.001f;
    engine::run_particles(world);
    auto& em = world.get<engine::render::ParticleEmitter>(emitter_entity);
    ASSERT_EQ(em.active_count(), 1u);
    em.particles[0].position = {0.0f, 0.0f, 2.0f};
    em.particles[0].velocity = {0.0f, 0.0f, -4.0f};

    world.ctx<engine::Time>().delta_time = 0.5f;
    engine::run_particles(world);
    ASSERT_EQ(em.active_count(), 1u);
    EXPECT_NEAR(em.particles[0].position.z, 0.0f, 0.01f);
    EXPECT_EQ(world.try_ctx<engine::render::ParticleGrid>(), nullptr);
}

void expect_same_particles(
        const engine::render::ParticleEmitter& serial, const engine::render::ParticleEmitter& pooled) {
    EXPECT_EQ(serial.particles.size(), pooled.particles.size());
    if (serial.particles.size() != pooled.particles.size()) {
        return;
    }
    for (std::size_t i = 0; i < serial.particles.size(); ++i) {
        const engine::render::Particle& a = serial.particles[i];
        const engine::render::Particle& b = pooled.particles[i];
        EXPECT_FLOAT_EQ(a.position.x, b.position.x);
        EXPECT_FLOAT_EQ(a.position.y, b.position.y);
        EXPECT_FLOAT_EQ(a.position.z, b.position.z);
        EXPECT_FLOAT_EQ(a.velocity.x, b.velocity.x);
        EXPECT_FLOAT_EQ(a.velocity.y, b.velocity.y);
        EXPECT_FLOAT_EQ(a.velocity.z, b.velocity.z);
        EXPECT_FLOAT_EQ(a.color.r, b.color.r);
        EXPECT_FLOAT_EQ(a.color.g, b.color.g);
        EXPECT_FLOAT_EQ(a.color.b, b.color.b);
        EXPECT_FLOAT_EQ(a.color.a, b.color.a);
        EXPECT_FLOAT_EQ(a.size.x, b.size.x);
        EXPECT_FLOAT_EQ(a.size.y, b.size.y);
        EXPECT_FLOAT_EQ(a.age, b.age);
        EXPECT_FLOAT_EQ(a.rotation, b.rotation);
    }
}

engine::render::ParticleEmitter seeded_emitter() {
    engine::render::ParticleEmitter emitter;
    emitter.emission_rate = 0.0f;
    emitter.playing = false;
    emitter.gravity = {0.0f, -20.0f, 0.0f};
    emitter.collision_enabled = true;
    emitter.bounce = 0.4f;
    emitter.friction = 0.2f;
    emitter.lifetime_loss = 0.0f;
    emitter.max_particles = 300;
    emitter.particles.reserve(200);
    for (int i = 0; i < 200; ++i) {
        engine::render::Particle particle;
        particle.position = {static_cast<float>(i) * 0.05f, 2.0f, 0.0f};
        particle.velocity = {0.2f, -40.0f - static_cast<float>(i % 5), 0.0f};
        particle.lifetime = (i % 7 == 0) ? 0.02f : 2.0f;
        particle.age = 0.0f;
        particle.start_size = {1.0f, 1.0f};
        particle.end_size = {0.2f, 0.2f};
        particle.base_size = particle.start_size;
        particle.start_color = {1.0f, 0.2f, 0.0f, 1.0f};
        particle.end_color = {0.0f, 0.0f, 1.0f, 0.0f};
        particle.color = particle.start_color;
        particle.size = particle.start_size;
        emitter.particles.push_back(particle);
    }
    return emitter;
}

TEST(ParticleEmitter, ParallelIntegrationMatchesTheCaller) {
    const std::vector<engine::render::ParticleCollider2D> colliders{
            engine::render::ParticleCollider2D{
                    .shape = engine::render::ParticleCollider2D::Shape::Box,
                    .position = {5.0f, 0.0f, 0.0f},
                    .box_size = {40.0f, 1.0f, 1.0f},
                    .layer = 1,
            },
    };
    const std::vector<engine::render::ParticleCollider3D> colliders_3d{
            engine::render::ParticleCollider3D{
                    .shape = engine::render::ParticleCollider3D::Shape::Box,
                    .position = {0.0f, 0.0f, -2.0f},
                    .box_size = {40.0f, 1.0f, 1.0f},
                    .layer = 1,
            },
    };
    const engine::render::ParticleEmitter seeded = seeded_emitter();
    engine::render::ParticleEmitter serial = seeded;
    engine::render::ParticleEmitter inline_jobs = seeded;
    engine::render::ParticleEmitter pooled = seeded;
    engine::JobSystem none(0);
    engine::JobSystem jobs(2);

    engine::render::update_emitter(serial, 0.05f, glm::mat4{1.0f}, colliders, colliders_3d);
    engine::render::update_emitter(inline_jobs, 0.05f, glm::mat4{1.0f}, colliders, colliders_3d, &none);
    engine::render::update_emitter(pooled, 0.05f, glm::mat4{1.0f}, colliders, colliders_3d, &jobs);

    expect_same_particles(serial, inline_jobs);
    expect_same_particles(serial, pooled);
    EXPECT_LT(serial.active_count(), seeded.active_count());
}

void expect_card(glm::vec3 actual, glm::vec3 expected) {
    EXPECT_NEAR(actual.x, expected.x, 1e-5f);
    EXPECT_NEAR(actual.y, expected.y, 1e-5f);
    EXPECT_NEAR(actual.z, expected.z, 1e-5f);
}

glm::vec3 card_offset(
        engine::render::ParticleDraw draw, glm::vec3 right, glm::vec3 up, glm::vec2 corner, glm::vec2 size,
        float rotation, glm::vec3 velocity) {
    return engine::render::particle_card_offset(draw, right, up, corner, size, rotation, velocity);
}

TEST(ParticleCard, OffsetMatchesCameraPlaneAndPlaneStaysXY) {
    using engine::render::ParticleDraw;
    const glm::vec3 side_right{0.0f, 0.0f, 1.0f};
    const glm::vec3 side_up{0.0f, 1.0f, 0.0f};
    const glm::vec3 identity_right{1.0f, 0.0f, 0.0f};
    const glm::vec3 identity_up{0.0f, 1.0f, 0.0f};
    const float right_angle = glm::radians(90.0f);

    expect_card(card_offset(ParticleDraw::Billboard, side_right, side_up, {0.5f, 0.5f}, {2.0f, 2.0f}, 0.0f, {}),
            {0.0f, 1.0f, 1.0f});
    expect_card(
            card_offset(ParticleDraw::Billboard, side_right, side_up, {0.5f, 0.5f}, {2.0f, 2.0f}, right_angle, {}),
            {0.0f, 1.0f, -1.0f});
    expect_card(card_offset(ParticleDraw::Plane, side_right, side_up, {0.5f, 0.5f}, {2.0f, 2.0f}, 0.0f, {}),
            {1.0f, 1.0f, 0.0f});
    expect_card(card_offset(ParticleDraw::Plane, side_right, side_up, {0.5f, 0.5f}, {2.0f, 2.0f}, right_angle, {}),
            {-1.0f, 1.0f, 0.0f});
    expect_card(
            card_offset(ParticleDraw::Billboard, identity_right, identity_up, {0.5f, -0.5f}, {2.0f, 4.0f}, 0.0f, {}),
            {1.0f, -2.0f, 0.0f});
    expect_card(card_offset(ParticleDraw::Stretched, identity_right, identity_up, {0.5f, 0.5f}, {2.0f, 2.0f}, 0.0f,
                        {0.0f, 4.0f, 0.0f}),
            {1.0f, 1.0f, 0.0f});
    expect_card(card_offset(ParticleDraw::Stretched, identity_right, identity_up, {0.0f, 0.5f}, {2.0f, 6.0f}, 0.0f,
                        {0.0f, -4.0f, 0.0f}),
            {0.0f, -3.0f, 0.0f});
    expect_card(card_offset(ParticleDraw::Stretched, identity_right, identity_up, {0.5f, 0.0f}, {2.0f, 6.0f}, 0.0f,
                        {0.0f, -4.0f, 0.0f}),
            {-1.0f, 0.0f, 0.0f});
    expect_card(card_offset(ParticleDraw::Stretched, identity_right, identity_up, {0.0f, 0.5f}, {2.0f, 6.0f}, 0.0f, {}),
            {0.0f, 3.0f, 0.0f});
    expect_card(card_offset(ParticleDraw::Stretched, identity_right, identity_up, {0.0f, 0.5f}, {2.0f, 6.0f}, 0.0f,
                        {1e-4f, 0.0f, 0.0f}),
            {0.0f, 3.0f, 0.0f});
    expect_card(card_offset(ParticleDraw::Stretched, identity_right, identity_up, {0.0f, 0.5f}, {2.0f, 6.0f}, 0.0f,
                        {2e-4f, 0.0f, 0.0f}),
            {3.0f, 0.0f, 0.0f});
    expect_card(card_offset(ParticleDraw::Stretched, identity_right, identity_up, {0.0f, 0.5f}, {2.0f, 6.0f}, 0.0f,
                        {0.0f, 0.0f, 9.0f}),
            {0.0f, 3.0f, 0.0f});
    expect_card(card_offset(ParticleDraw::Billboard, {2.0f, 0.0f, 0.0f}, {0.0f, 4.0f, 0.0f}, {0.5f, 0.5f},
                        {2.0f, 2.0f}, 0.0f, {}),
            {1.0f, 1.0f, 0.0f});
    expect_card(card_offset(ParticleDraw::Billboard, {}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.0f}, {2.0f, 2.0f}, 0.0f, {}),
            {1.0f, 0.0f, 0.0f});
}

TEST(ParticleInstance, LayoutKeepsPlaneAttributesAndAppendsVelocity) {
    EXPECT_EQ(offsetof(engine::render::ParticleInstance, position), 0u);
    EXPECT_EQ(offsetof(engine::render::ParticleInstance, rotation), 12u);
    EXPECT_EQ(offsetof(engine::render::ParticleInstance, size), 16u);
    EXPECT_EQ(offsetof(engine::render::ParticleInstance, color), 24u);
    EXPECT_EQ(offsetof(engine::render::ParticleInstance, uv_scale), 40u);
    EXPECT_EQ(offsetof(engine::render::ParticleInstance, uv_offset), 48u);
    EXPECT_EQ(offsetof(engine::render::ParticleInstance, velocity), 56u);
    EXPECT_EQ(sizeof(engine::render::ParticleInstance), 68u);
}

TEST(ParticleSystemRender, StretchedVelocityStaysWorldAndFollowsLocal) {
    engine::ecs::World world;
    spawn_camera(world);
    engine::render::CommandBuffer commands;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});

    const auto material = std::make_shared<FakeMaterial>();
    const auto add = [&](engine::render::SimulationSpace space) {
        const engine::ecs::Entity entity = world.create();
        engine::Transform transform;
        transform.rotation = engine::rotation_z(glm::radians(90.0f));
        world.emplace<engine::Transform>(entity, transform);
        auto& emitter = world.emplace<engine::render::ParticleEmitter>(entity);
        emitter.draw = engine::render::ParticleDraw::Stretched;
        emitter.simulation_space = space;
        emitter.mesh = std::make_shared<FakeMesh>();
        emitter.material = material;
        emitter.emission_rate = 0.0f;
        engine::render::Particle particle;
        particle.velocity = {4.0f, 0.0f, 0.0f};
        particle.lifetime = 1.0f;
        emitter.particles.push_back(particle);
    };
    add(engine::render::SimulationSpace::World);
    add(engine::render::SimulationSpace::Local);

    world.ctx<engine::Time>().delta_time = 0.0f;
    world.run(engine::ecs::Schedule::Frame);

    glm::vec3 world_velocity{0.0f};
    glm::vec3 local_velocity{0.0f};
    int draws = 0;
    for (std::size_t i = 0; i < commands.size(); ++i) {
        if (!std::holds_alternative<engine::render::CmdDrawParticles>(commands[i])) {
            continue;
        }
        const auto& cmd = std::get<engine::render::CmdDrawParticles>(commands[i]);
        EXPECT_EQ(cmd.draw, engine::render::ParticleDraw::Stretched);
        ASSERT_EQ(cmd.instances.size(), 1u);
        ++draws;
        if (cmd.instances[0].velocity.x > 1.0f) {
            world_velocity = cmd.instances[0].velocity;
        } else {
            local_velocity = cmd.instances[0].velocity;
        }
    }
    EXPECT_EQ(draws, 2);
    EXPECT_FLOAT_EQ(world_velocity.x, 4.0f);
    EXPECT_FLOAT_EQ(world_velocity.y, 0.0f);
    EXPECT_FLOAT_EQ(world_velocity.z, 0.0f);
    EXPECT_NEAR(local_velocity.x, 0.0f, 1e-5f);
    EXPECT_NEAR(local_velocity.y, 4.0f, 1e-5f);
    EXPECT_NEAR(local_velocity.z, 0.0f, 1e-5f);
}

TEST(ParticleMesh, InstanceTransformScalesByTheFactorAndIgnoresCardSize) {
    const engine::render::MeshParticleInstance formed = engine::render::particle_mesh_instance(
            {1.0f, 2.0f, 3.0f}, {2.0f, 4.0f, 8.0f}, 0.5f, {0.25f, 0.5f, 0.75f, 1.0f}, 3.0f);
    EXPECT_EQ(formed.position, (glm::vec3{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(formed.scale, (glm::vec3{1.0f, 2.0f, 4.0f}));
    EXPECT_EQ(formed.color, (glm::vec4{0.25f, 0.5f, 0.75f, 1.0f}));
    EXPECT_FLOAT_EQ(formed.layer, 3.0f);

    EXPECT_FLOAT_EQ(engine::render::particle_mesh_size_factor({}, 0.4f, 1.0f), 1.0f);
    const engine::render::Curve<float> swell = engine::render::Curve<float>::linear(4.0f, 0.0f);
    EXPECT_FLOAT_EQ(engine::render::particle_mesh_size_factor(swell, 0.25f, 1.0f), 3.0f);
    const engine::render::Curve<float> held = engine::render::Curve<float>::constant(2.0f);
    EXPECT_FLOAT_EQ(engine::render::particle_mesh_size_factor(held, 0.5f, 0.0f), 2.0f);
}

TEST(ParticleMesh, SpawnCopiesTextureLayerAndLeavesItOnTheLiveParticle) {
    engine::render::ParticleEmitter emitter;
    emitter.emission_rate = 0.0f;
    emitter.texture_layer = 6.0f;
    emitter.burst(1);
    engine::render::update_emitter(emitter, 0.1f);
    ASSERT_EQ(emitter.particles.size(), 1u);
    EXPECT_FLOAT_EQ(emitter.particles[0].texture_layer, 6.0f);

    emitter.texture_layer = 0.0f;
    engine::render::update_emitter(emitter, 0.1f);
    ASSERT_EQ(emitter.particles.size(), 1u);
    EXPECT_FLOAT_EQ(emitter.particles[0].texture_layer, 6.0f);
}

namespace {

constexpr int kShapeSamples = 128;
constexpr float kShapeEpsilon = 1e-4f;
constexpr float kFixtureEpsilon = 1e-5f;

void expect_offset(glm::vec3 actual, glm::vec3 expected) {
    EXPECT_NEAR(actual.x, expected.x, kFixtureEpsilon);
    EXPECT_NEAR(actual.y, expected.y, kFixtureEpsilon);
    EXPECT_NEAR(actual.z, expected.z, kFixtureEpsilon);
}

engine::render::ParticleEmitter spawn_shape(
        engine::render::EmitterShape shape, glm::vec3 scale, glm::vec3 direction) {
    engine::render::ParticleEmitter emitter;
    emitter.emission_rate = 0.0f;
    emitter.max_particles = static_cast<std::size_t>(kShapeSamples);
    emitter.lifetime_min = 10.0f;
    emitter.lifetime_max = 10.0f;
    emitter.speed_min = 0.0f;
    emitter.speed_max = 0.0f;
    emitter.gravity = {0.0f, 0.0f, 0.0f};
    emitter.collision_enabled = false;
    emitter.spread_angle = 0.0f;
    emitter.shape = shape;
    emitter.shape_scale = scale;
    emitter.direction = direction;
    emitter.burst(kShapeSamples);
    engine::render::update_emitter(emitter, 0.016f);
    return emitter;
}

}

TEST(ParticleShape, SphereFixturesAreVolumePoints) {
    const glm::vec3 scale{3.0f, 1.0f, 2.0f};
    const glm::vec3 unused_direction{0.0f, 1.0f, 0.0f};
    expect_offset(
            engine::render::particle_shape_offset(
                    engine::render::EmitterShape::Sphere, scale, unused_direction, 1.0f, 0.5f, 0.0f),
            {3.0f, 0.0f, 0.0f});
    // r = cbrt(0.125) = 0.5, so this is inside the ball. A surface sample would be (0, 1, 0).
    expect_offset(
            engine::render::particle_shape_offset(
                    engine::render::EmitterShape::Sphere, scale, unused_direction, 0.125f, 0.5f, 0.25f),
            {0.0f, 0.5f, 0.0f});
    expect_offset(
            engine::render::particle_shape_offset(
                    engine::render::EmitterShape::Sphere, scale, unused_direction, 1.0f, 1.0f, 0.0f),
            {0.0f, 0.0f, 2.0f});
    expect_offset(
            engine::render::particle_shape_offset(
                    engine::render::EmitterShape::Sphere, {-3.0f, 1.0f, 2.0f}, unused_direction, 1.0f, 0.5f, 0.0f),
            {-3.0f, 0.0f, 0.0f});
}

TEST(ParticleShape, DiscFixturesLieOnTheDirectionPlane) {
    const glm::vec3 scale{4.0f, 1.0f, 9.0f};
    // Normal +Z, angle 0: tangent is +X, so the point is (scale.x, 0, 0). scale.z is unused.
    expect_offset(
            engine::render::particle_shape_offset(
                    engine::render::EmitterShape::Disc, scale, {0.0f, 0.0f, 1.0f}, 1.0f, 0.0f, 0.0f),
            {4.0f, 0.0f, 0.0f});
    // Normal +Y (and the zero fallback): helper is +X, tangent is +Z.
    expect_offset(
            engine::render::particle_shape_offset(
                    engine::render::EmitterShape::Disc, scale, {0.0f, 1.0f, 0.0f}, 1.0f, 0.0f, 0.0f),
            {0.0f, 0.0f, 4.0f});
    expect_offset(
            engine::render::particle_shape_offset(
                    engine::render::EmitterShape::Disc, scale, {0.0f, 0.0f, 0.0f}, 1.0f, 0.0f, 0.0f),
            {0.0f, 0.0f, 4.0f});
    // length^2 = 1e-8, under 1e-6, so the direction is replaced with +Y.
    expect_offset(
            engine::render::particle_shape_offset(
                    engine::render::EmitterShape::Disc, scale, {1.0e-4f, 0.0f, 0.0f}, 1.0f, 0.0f, 0.0f),
            {0.0f, 0.0f, 4.0f});
    // Normal +X. helper is +Y, tangent is (0, 0, -1), angle 0 lands on (0, 0, -scale.x).
    expect_offset(
            engine::render::particle_shape_offset(
                    engine::render::EmitterShape::Disc, scale, {1.0f, 0.0f, 0.0f}, 1.0f, 0.0f, 0.0f),
            {0.0f, 0.0f, -4.0f});
    // length^2 = 1e-4, above 1e-6, so this stays +X and hits the same point.
    expect_offset(
            engine::render::particle_shape_offset(
                    engine::render::EmitterShape::Disc, scale, {1.0e-2f, 0.0f, 0.0f}, 1.0f, 0.0f, 0.0f),
            {0.0f, 0.0f, -4.0f});
}

TEST(ParticleShape, SphereSamplesStayInsideTheEllipsoid) {
    const engine::render::ParticleEmitter emitter =
            spawn_shape(engine::render::EmitterShape::Sphere, {3.0f, 1.0f, 2.0f}, {0.0f, 1.0f, 0.0f});
    ASSERT_EQ(emitter.particles.size(), static_cast<std::size_t>(kShapeSamples));
    for (const engine::render::Particle& particle : emitter.particles) {
        const glm::vec3 p = particle.position;
        EXPECT_TRUE(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
        const float nx = p.x / 3.0f;
        const float ny = p.y / 1.0f;
        const float nz = p.z / 2.0f;
        EXPECT_LE(nx * nx + ny * ny + nz * nz, 1.0f + kShapeEpsilon);
    }
}

TEST(ParticleShape, DiscSamplesStayOnThePlane) {
    const glm::vec3 direction{1.0f, 2.0f, 2.0f};
    const glm::vec3 normal = glm::normalize(direction);
    const engine::render::ParticleEmitter emitter =
            spawn_shape(engine::render::EmitterShape::Disc, {4.0f, 1.0f, 9.0f}, direction);
    ASSERT_EQ(emitter.particles.size(), static_cast<std::size_t>(kShapeSamples));
    for (const engine::render::Particle& particle : emitter.particles) {
        const glm::vec3 p = particle.position;
        EXPECT_LE(std::abs(glm::dot(p, normal)), kShapeEpsilon);
        EXPECT_LE(glm::length(p), 4.0f + kShapeEpsilon);
    }
}

TEST(ParticleShape, DiscZeroDirectionUsesUp) {
    const engine::render::ParticleEmitter emitter =
            spawn_shape(engine::render::EmitterShape::Disc, {4.0f, 1.0f, 9.0f}, {0.0f, 0.0f, 0.0f});
    ASSERT_EQ(emitter.particles.size(), static_cast<std::size_t>(kShapeSamples));
    for (const engine::render::Particle& particle : emitter.particles) {
        const glm::vec3 p = particle.position;
        EXPECT_LE(std::abs(p.y), kShapeEpsilon);
        EXPECT_LE(glm::length(p), 4.0f + kShapeEpsilon);
    }
}

TEST(ParticleShape, CircleSamplesStayOnXy) {
    const engine::render::ParticleEmitter emitter =
            spawn_shape(engine::render::EmitterShape::Circle, {2.5f, 0.5f, 8.0f}, {0.0f, 1.0f, 0.0f});
    ASSERT_EQ(emitter.particles.size(), static_cast<std::size_t>(kShapeSamples));
    for (const engine::render::Particle& particle : emitter.particles) {
        const glm::vec3 p = particle.position;
        EXPECT_FLOAT_EQ(p.z, 0.0f);
        const float nx = p.x / 2.5f;
        const float ny = p.y / 0.5f;
        EXPECT_LE(nx * nx + ny * ny, 1.0f + kShapeEpsilon);
    }
}

TEST(ParticleMesh, VertexSourceDeclaresInstanceInputsByName) {
    const std::string_view present = "uniform int uInstanced; layout(location = 7) in vec3 aInstancePos; "
                                      "layout(location = 8) in vec3 aInstanceScale; "
                                      "layout(location = 9) in vec4 aInstanceColor;";
    EXPECT_TRUE(engine::render::shader_declares_particle_instance_inputs(present));
    const std::string_view absent = "layout(location = 0) in vec3 aPosition; uniform mat4 uModel;";
    EXPECT_FALSE(engine::render::shader_declares_particle_instance_inputs(absent));
    EXPECT_FALSE(engine::render::shader_declares_particle_instance_inputs("aInstancePosition uInstancedExtra"));
}

