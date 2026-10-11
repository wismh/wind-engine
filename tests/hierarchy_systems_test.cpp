#include <gtest/gtest.h>

#include <engine/core/bound_windows.h>
#include <engine/core/time.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/physics.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/render/command_buffer.h>
#include <engine/render/material.h>
#include <engine/render/particles.h>
#include <engine/render/renderable.h>
#include <engine/render/sprite.h>
#include <engine/ui/canvas.h>
#include <engine/ui/presentation.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace {

using engine::Transform;
using engine::ecs::Entity;
using engine::ecs::Phase;
using engine::ecs::Schedule;
using engine::ecs::World;

class FakeMesh final : public engine::render::IMesh {};

class FakeMaterial final : public engine::render::IMaterial {
public:
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
        return engine::render::BlendMode::Alpha;
    }
};

Entity spawn(World& world, Transform transform) {
    const Entity entity = world.create();
    world.emplace<Transform>(entity, transform);
    return entity;
}

Entity spawn_child(World& world, Entity parent, Transform transform) {
    const Entity entity = spawn(world, transform);
    EXPECT_TRUE(engine::ecs::set_parent(world, entity, parent));
    return entity;
}

void expect_near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

void expect_near(const glm::mat4& a, const glm::mat4& b, float eps = 1e-4f) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            EXPECT_NEAR(a[c][r], b[c][r], eps) << "column " << c << " row " << r;
        }
    }
}

Entity bind_camera(World& world, Entity camera) {
    world.emplace<engine::Camera>(camera, engine::Camera{.ortho_size = 5.0f});
    world.ctx<engine::ActiveCamera>().entity = camera;
    engine::ui::presentation_of(world).sizes.sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    world.ctx<engine::BoundWindows>().ids = {engine::kPrimaryWindow};
    return camera;
}

void step_physics(World& world) {
    world.ctx<engine::Time>().fixed_delta_time = engine::kFixed;
    world.run(Schedule::Fixed);
}

std::vector<engine::CollisionEvent> read_collisions(World& world) {
    std::vector<engine::CollisionEvent> events;
    for (const engine::CollisionEvent& event : engine::ecs::EventReader<engine::CollisionEvent>{world}) {
        events.push_back(event);
    }
    return events;
}

const engine::render::CmdDrawMesh& mesh_command(const engine::render::CommandBuffer& commands, std::size_t index) {
    EXPECT_LT(index, commands.size());
    return std::get<engine::render::CmdDrawMesh>(commands[index]);
}

}

TEST(HierarchySystems, TransformPhaseRunsBetweenGameAndBind) {
    World world;
    engine::register_engine_systems(world);

    const Entity parent = spawn(world, Transform{.position = {1.f, 0.f, 0.f}});
    const Entity child = spawn_child(world, parent, Transform{.position = {2.f, 0.f, 0.f}});
    engine::ecs::update_world_transforms(world);

    std::vector<std::string> order;
    float game_saw = 0.f;
    float bind_saw = 0.f;
    world.add_system(Schedule::Frame, Phase::Game, [&](World& w) {
        order.push_back("Game");
        w.get<Transform>(parent).position.x = 10.f;
        game_saw = engine::ecs::world_position(w, child).x;
    });
    world.add_system(Schedule::Frame, Phase::Bind, [&](World& w) {
        order.push_back("Bind");
        bind_saw = engine::ecs::world_position(w, child).x;
    });

    world.run(Schedule::Frame);

    EXPECT_EQ(order, (std::vector<std::string>{"Game", "Bind"}));
    EXPECT_FLOAT_EQ(game_saw, 3.f);   // Game still sees the previous frame's world matrix
    EXPECT_FLOAT_EQ(bind_saw, 12.f);  // Transform ran after Game, before Bind
    EXPECT_EQ(engine::ecs::kFramePhases[2], Phase::Transform);
}

TEST(HierarchySystems, TransformPhaseIsNotInFixedSchedule) {
    for (const Phase phase : engine::ecs::kFixedPhases) {
        EXPECT_NE(phase, Phase::Transform);
    }
}

TEST(HierarchySystems, ChildSpriteModelIsParentTimesLocal) {
    engine::render::CommandBuffer commands;
    World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    bind_camera(world, spawn(world, Transform{.position = {0.f, 0.f, 10.f}}));

    const Transform parent_transform{
            .position = {3.f, 1.f, 0.f}, .rotation = {0.f, 0.f, 0.5f}, .scale = {2.f, 2.f, 1.f}};
    const Transform child_transform{.position = {1.f, 2.f, 0.f}, .rotation = {0.f, 0.f, 0.25f}};
    const Entity parent = spawn(world, parent_transform);
    const Entity child = spawn_child(world, parent, child_transform);
    engine::render::Sprite sprite;
    sprite.mesh = std::make_shared<FakeMesh>();
    sprite.material = std::make_shared<FakeMaterial>();
    world.emplace<engine::render::Sprite>(child, sprite);

    world.run(Schedule::Frame);

    ASSERT_EQ(commands.size(), 1u);
    expect_near(mesh_command(commands, 0).model,
                engine::model_matrix(parent_transform) * engine::model_matrix(child_transform));
}

TEST(HierarchySystems, ChildRenderableModelFollowsMovedParent) {
    engine::render::CommandBuffer commands;
    World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    bind_camera(world, spawn(world, Transform{.position = {0.f, 0.f, 10.f}}));

    const Entity parent = spawn(world, Transform{});
    const Entity child = spawn_child(world, parent, Transform{.position = {1.f, 0.f, 0.f}});
    world.emplace<engine::render::Renderable>(child, engine::render::Renderable{std::make_shared<FakeMesh>(),
            std::make_shared<FakeMaterial>(), glm::vec4{1.f}, 0, 0});

    world.run(Schedule::Frame);
    world.get<Transform>(parent).position = {5.f, 0.f, 0.f};
    commands.clear();
    world.run(Schedule::Frame);

    ASSERT_EQ(commands.size(), 1u);
    expect_near(glm::vec3(mesh_command(commands, 0).model[3]), {6.f, 0.f, 0.f});
}

TEST(HierarchySystems, ChildColliderOverlapsInWorldCoordinates) {
    World world;
    engine::register_engine_systems(world);

    const Entity parent = spawn(world, Transform{.position = {10.f, 0.f, 0.f}});
    const Entity child = spawn_child(world, parent, Transform{.position = {0.5f, 0.f, 0.f}});
    world.emplace<engine::BoxCollider>(child, engine::BoxCollider{glm::vec3{2.f, 2.f, 1.f}});
    const Entity other = spawn(world, Transform{.position = {10.5f, 0.f, 0.f}});
    world.emplace<engine::BoxCollider>(other, engine::BoxCollider{glm::vec3{2.f, 2.f, 1.f}});
    const Entity far_root = spawn(world, Transform{.position = {0.5f, 0.f, 0.f}});
    world.emplace<engine::BoxCollider>(far_root, engine::BoxCollider{glm::vec3{2.f, 2.f, 1.f}});

    step_physics(world);

    const std::vector<engine::CollisionEvent> events = read_collisions(world);
    ASSERT_EQ(events.size(), 1u);
    const bool pair_ok = (events[0].a == child && events[0].b == other) || (events[0].a == other && events[0].b == child);
    EXPECT_TRUE(pair_ok);
}

TEST(HierarchySystems, ChildColliderUsesParentMovedThisStep) {
    World world;
    engine::register_engine_systems(world);

    const Entity parent = spawn(world, Transform{});
    world.emplace<engine::RigidBody>(parent, engine::RigidBody{glm::vec3{60.f, 0.f, 0.f}});
    const Entity child = spawn_child(world, parent, Transform{});
    world.emplace<engine::CircleCollider>(child, engine::CircleCollider{.radius = 0.5f});
    const Entity other = spawn(world, Transform{.position = {1.f, 0.f, 0.f}});
    world.emplace<engine::CircleCollider>(other, engine::CircleCollider{.radius = 0.5f});

    // Parent integrates to x = 1 this step; only a fresh world position puts the child on top of `other`.
    step_physics(world);

    EXPECT_EQ(read_collisions(world).size(), 1u);
}

TEST(HierarchySystems, LocalSpaceEmitterOnChildFollowsParent) {
    engine::render::CommandBuffer commands;
    World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    bind_camera(world, spawn(world, Transform{.position = {0.f, 0.f, 10.f}}));
    world.ctx<engine::Time>().delta_time = 0.016f;

    const Entity parent = spawn(world, Transform{.position = {10.f, 0.f, 0.f}});
    const Entity child = spawn_child(world, parent, Transform{.position = {2.f, 0.f, 0.f}});
    engine::render::ParticleEmitter emitter;
    emitter.mesh = std::make_shared<FakeMesh>();
    emitter.material = std::make_shared<FakeMaterial>();
    emitter.simulation_space = engine::render::SimulationSpace::Local;
    emitter.emission_rate = 0.f;
    emitter.lifetime_min = 5.f;
    emitter.lifetime_max = 5.f;
    emitter.speed_min = 0.f;
    emitter.speed_max = 0.f;
    emitter.burst(1);
    world.emplace<engine::render::ParticleEmitter>(child, std::move(emitter));

    world.run(Schedule::Frame);
    ASSERT_EQ(commands.size(), 1u);
    const auto& first = std::get<engine::render::CmdDrawParticles>(commands[0]);
    ASSERT_EQ(first.instances.size(), 1u);
    EXPECT_NEAR(first.instances[0].position.x, 12.f, 1e-3f);

    world.get<Transform>(parent).position.x = 20.f;
    commands.clear();
    world.run(Schedule::Frame);
    ASSERT_EQ(commands.size(), 1u);
    const auto& second = std::get<engine::render::CmdDrawParticles>(commands[0]);
    ASSERT_EQ(second.instances.size(), 1u);
    EXPECT_NEAR(second.instances[0].position.x, 22.f, 1e-3f);
}

TEST(HierarchySystems, CameraChildOfMovingEntityGivesWorldView) {
    World world;
    engine::register_engine_systems(world);
    const Entity rig = spawn(world, Transform{.position = {5.f, 0.f, 0.f}});
    const Entity camera = bind_camera(world, spawn_child(world, rig, Transform{.position = {1.f, 0.f, 0.f}}));
    engine::ecs::update_world_transforms(world);

    const glm::mat4 camera_world = engine::ecs::world_matrix(world, camera);
    const glm::vec4 origin = engine::view_matrix(camera_world) * glm::vec4(6.f, 0.f, 0.f, 1.f);
    expect_near(glm::vec3(origin), {0.f, 0.f, 0.f});

    const engine::ui::WindowSize window{800, 600};
    const glm::vec3 center =
            engine::screen_to_world({400.f, 300.f}, world.get<engine::Camera>(camera), camera_world, window);
    expect_near(center, {6.f, 0.f, 0.f});
    const glm::vec2 screen = engine::world_to_screen({6.f, 0.f, 0.f}, world.get<engine::Camera>(camera), camera_world, window);
    EXPECT_NEAR(screen.x, 400.f, 1e-3f);
    EXPECT_NEAR(screen.y, 300.f, 1e-3f);

    world.get<Transform>(rig).position.x = 15.f;
    engine::ecs::update_world_transforms(world);
    const glm::vec3 moved = engine::screen_to_world(
            {400.f, 300.f}, world.get<engine::Camera>(camera), engine::ecs::world_matrix(world, camera), window);
    expect_near(moved, {16.f, 0.f, 0.f});
}

TEST(HierarchySystems, RotatedCameraRotatesTheView) {
    engine::Camera camera;
    const glm::mat4 camera_world = glm::rotate(glm::mat4(1.f), glm::half_pi<float>(), glm::vec3(0.f, 0.f, 1.f));
    const engine::ui::WindowSize window{800, 600};
    // The camera's local +X axis now points along world +Y, so the screen's right edge looks along world +Y.
    const glm::vec3 right = engine::screen_to_world({800.f, 300.f}, camera, camera_world, window);
    EXPECT_NEAR(right.x, 0.f, 1e-3f);
    EXPECT_GT(right.y, 1.f);
}

TEST(HierarchySystems, RenderUsesWorldMatrixOfParentedCamera) {
    engine::render::CommandBuffer commands;
    World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    const Entity rig = spawn(world, Transform{.position = {5.f, 0.f, 0.f}});
    bind_camera(world, spawn_child(world, rig, Transform{.position = {1.f, 0.f, 10.f}}));
    const Entity renderable = spawn(world, Transform{});
    world.emplace<engine::render::Renderable>(renderable, engine::render::Renderable{std::make_shared<FakeMesh>(),
            std::make_shared<FakeMaterial>(), glm::vec4{1.f}, 0, 0});

    world.run(Schedule::Frame);

    ASSERT_EQ(commands.size(), 1u);
    expect_near(mesh_command(commands, 0).view, glm::translate(glm::mat4(1.f), glm::vec3(-6.f, 0.f, -10.f)));
}

TEST(HierarchySystems, ChildRotationAboutZCarriesParentOffsetIn3D) {
    World world;
    engine::register_engine_systems(world);

    const Entity parent = spawn(world, Transform{.rotation = {0.f, 0.f, glm::half_pi<float>()}});
    const Entity child = spawn_child(world, parent, Transform{.position = {2.f, 0.f, 0.f}});
    const Entity grandchild = spawn_child(world, child, Transform{.position = {0.f, 1.f, 0.f}});
    engine::ecs::update_world_transforms(world);

    // Rotating (2,0,0) by 90 degrees about Z gives (0,2,0); the grandchild's local +Y then points along world -X.
    expect_near(engine::ecs::world_position(world, child), {0.f, 2.f, 0.f});
    expect_near(engine::ecs::world_position(world, grandchild), {-1.f, 2.f, 0.f});

    world.get<Transform>(parent).rotation = {glm::half_pi<float>(), 0.f, 0.f};
    world.get<Transform>(child).position = {0.f, 1.f, 0.f};
    engine::ecs::update_world_transforms(world);
    // 90 degrees about X sends local +Y to world +Z.
    expect_near(engine::ecs::world_position(world, child), {0.f, 0.f, 1.f});
}
