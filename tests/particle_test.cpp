#include <gtest/gtest.h>

#include <engine/core/time.h>
#include <engine/ecs/camera.h>
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
    world.ctx<engine::ui::WindowSize>().width = 800;
    world.ctx<engine::ui::WindowSize>().height = 600;
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
