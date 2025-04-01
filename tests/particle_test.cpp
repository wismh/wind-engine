#include <gtest/gtest.h>

#include <engine/core/time.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/physics.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/render/command_buffer.h>
#include <engine/render/particles.h>
#include <engine/render/sprite.h>
#include <engine/ui/canvas.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <memory>
#include <vector>

namespace {

class FakeMesh final : public engine::render::IMesh {};

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

    engine::render::BlendMode blend() const override {
        return blend_;
    }

private:
    engine::render::BlendMode blend_;
};

void spawn_camera(engine::ecs::World& world) {
    const engine::ecs::Entity camera = world.create();
    world.emplace<engine::Transform>(camera, engine::Transform{.position = {0.0f, 0.0f, 10.0f}});
    world.emplace<engine::Camera>(camera, engine::Camera{.ortho_size = 5.0f});
    world.ctx<engine::ActiveCamera>().entity = camera;
    world.ctx<engine::ui::WindowSizes>().sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
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

    ASSERT_EQ(commands.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawParticles>(commands[0]));

    const auto& cmd = std::get<engine::render::CmdDrawParticles>(commands[0]);
    EXPECT_EQ(cmd.mesh, em.mesh);
    EXPECT_EQ(cmd.material, em.material);
    EXPECT_EQ(cmd.blend, engine::render::BlendMode::Additive);
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

    ASSERT_EQ(commands.size(), 3u);
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(commands[0]));
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawParticles>(commands[1]));
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(commands[2]));
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
    std::vector<engine::render::ParticleCollider> colliders{
            engine::render::ParticleCollider{
                    .shape = engine::render::ParticleCollider::Shape::Box,
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
    std::vector<engine::render::ParticleCollider> colliders{
            engine::render::ParticleCollider{
                    .shape = engine::render::ParticleCollider::Shape::Circle,
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
    std::vector<engine::render::ParticleCollider> colliders{
            engine::render::ParticleCollider{
                    .shape = engine::render::ParticleCollider::Shape::Box,
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
    std::vector<engine::render::ParticleCollider> colliders{
            engine::render::ParticleCollider{
                    .shape = engine::render::ParticleCollider::Shape::Box,
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
    std::vector<engine::render::ParticleCollider> colliders{
            engine::render::ParticleCollider{
                    .shape = engine::render::ParticleCollider::Shape::Box,
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

TEST(ParticleSystemECS, RunParticlesCollidesWithWorldEntities) {
    engine::ecs::World world;
    world.ctx<engine::Time>().delta_time = 0.5f;

    // Spawn an obstacle entity in the ECS world with BoxCollider
    const engine::ecs::Entity floor = world.create();
    world.emplace<engine::Transform>(floor, engine::Transform{.position = {0.0f, 0.0f, 0.0f}});
    world.emplace<engine::BoxCollider>(floor, engine::BoxCollider{
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

