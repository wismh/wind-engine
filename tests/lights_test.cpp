#include <gtest/gtest.h>

#include <engine/core/bound_windows.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/render/ambient_light.h>
#include <engine/render/command_buffer.h>
#include <engine/render/directional_light.h>
#include <engine/render/lights.h>
#include <engine/render/material.h>
#include <engine/render/renderable.h>
#include <engine/render/scene_lights.h>
#include <engine/ui/presentation.h>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <variant>

namespace {

using engine::ecs::Entity;
using engine::render::AmbientLight;
using engine::render::DirectionalLight;
using engine::render::SceneLights;

class FakeMesh final : public engine::render::IMesh {};

class FakeMaterial final : public engine::render::IMaterial {
public:
    std::shared_ptr<engine::render::IShader> shader() const override { return {}; }
    std::shared_ptr<engine::render::ITexture> texture(int) const override { return {}; }
    glm::vec4 color() const override { return glm::vec4{1.0f}; }
    engine::render::RenderState render_state() const override { return {}; }
};

void expect_near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-5f) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

Entity spawn_directional(engine::ecs::World& world, engine::Transform transform, DirectionalLight light = {}) {
    const Entity entity = world.create();
    world.emplace<engine::Transform>(entity, transform);
    world.emplace<DirectionalLight>(entity, light);
    return entity;
}

engine::Transform turned(float angle, glm::vec3 axis) {
    engine::Transform transform;
    transform.rotation = glm::angleAxis(angle, axis);
    return transform;
}

}

TEST(Lights, NoLightEntitiesGiveTheDefaultRig) {
    engine::ecs::World world;
    const SceneLights lights = engine::render::collect_scene_lights(world);

    EXPECT_EQ(lights, engine::render::default_scene_lights());
    ASSERT_EQ(lights.directional_count, 1);
    expect_near(lights.directions[0], glm::normalize(engine::render::kDefaultLightDirection));
    EXPECT_NEAR(glm::length(lights.directions[0]), 1.0f, 1e-6f);
    EXPECT_LT(lights.directions[0].y, 0.0f) << "the default light shines down";
    expect_near(lights.colors[0], engine::render::kDefaultLightColor);
    expect_near(lights.ambient, engine::render::kDefaultAmbient);
}

TEST(Lights, ComponentDefaults) {
    const DirectionalLight directional;
    expect_near(directional.color, glm::vec3{1.0f});
    EXPECT_FLOAT_EQ(directional.intensity, 1.0f);
    const AmbientLight ambient;
    expect_near(ambient.color, glm::vec3{1.0f});
    EXPECT_FLOAT_EQ(ambient.intensity, 0.2f);
    EXPECT_EQ(engine::render::kMaxDirectionalLights, 4u);
}

TEST(Lights, DirectionIsTheWorldForwardOfTheRotation) {
    engine::ecs::World world;
    // A quarter turn about +Y takes the forward -Z to -X.
    spawn_directional(world, turned(glm::half_pi<float>(), {0.0f, 1.0f, 0.0f}),
            DirectionalLight{.color = {1.0f, 0.5f, 0.25f}, .intensity = 2.0f});

    const SceneLights lights = engine::render::collect_scene_lights(world);
    ASSERT_EQ(lights.directional_count, 1);
    expect_near(lights.directions[0], {-1.0f, 0.0f, 0.0f});
    expect_near(lights.colors[0], {2.0f, 1.0f, 0.5f});
    expect_near(lights.ambient, glm::vec3{0.0f});
}

TEST(Lights, UnrotatedLightAndLightWithoutTransformShineAlongMinusZ) {
    engine::ecs::World world;
    engine::Transform moved;
    moved.position = {3.0f, 4.0f, 5.0f};
    moved.scale = {2.0f, 2.0f, 2.0f};
    spawn_directional(world, moved);
    const Entity bare = world.create();
    world.emplace<DirectionalLight>(bare);

    const SceneLights lights = engine::render::collect_scene_lights(world);
    ASSERT_EQ(lights.directional_count, 2);
    expect_near(lights.directions[0], {0.0f, 0.0f, -1.0f});
    expect_near(lights.directions[1], {0.0f, 0.0f, -1.0f});
}

TEST(Lights, DirectionFollowsTheParent) {
    engine::ecs::World world;
    const Entity rig = world.create();
    // A quarter turn about +X takes -Z to +Y. The scale must not lengthen the direction.
    engine::Transform rig_transform = turned(glm::half_pi<float>(), {1.0f, 0.0f, 0.0f});
    rig_transform.scale = {3.0f, 3.0f, 3.0f};
    world.emplace<engine::Transform>(rig, rig_transform);
    const Entity light = spawn_directional(world, engine::Transform{});
    ASSERT_TRUE(engine::ecs::set_parent(world, light, rig));
    engine::ecs::update_world_transforms(world);

    const SceneLights lights = engine::render::collect_scene_lights(world);
    ASSERT_EQ(lights.directional_count, 1);
    expect_near(lights.directions[0], {0.0f, 1.0f, 0.0f});
}

TEST(Lights, ExtraDirectionalLightsAreCutInEntityIndexOrder) {
    engine::ecs::World world;
    std::array<Entity, 6> entities{};
    for (Entity& entity : entities) {
        entity = world.create();
    }
    // Added in reverse, so storage order is not entity order.
    for (std::size_t i = entities.size(); i-- > 0;) {
        world.emplace<DirectionalLight>(entities[i], DirectionalLight{.intensity = static_cast<float>(i + 1)});
    }

    const SceneLights lights = engine::render::collect_scene_lights(world);
    ASSERT_EQ(lights.directional_count, static_cast<int>(engine::render::kMaxDirectionalLights));
    for (std::size_t i = 0; i < engine::render::kMaxDirectionalLights; ++i) {
        expect_near(lights.colors[i], glm::vec3{static_cast<float>(i + 1)});
    }
    // The cut is the same every frame.
    EXPECT_EQ(engine::render::collect_scene_lights(world), lights);
}

TEST(Lights, AmbientIsTheSumOfEveryAmbientLight) {
    engine::ecs::World world;
    world.emplace<AmbientLight>(world.create(), AmbientLight{.color = {1.0f, 0.0f, 0.0f}, .intensity = 0.25f});
    world.emplace<AmbientLight>(world.create(), AmbientLight{.color = {0.0f, 1.0f, 1.0f}, .intensity = 0.5f});

    const SceneLights lights = engine::render::collect_scene_lights(world);
    expect_near(lights.ambient, {0.25f, 0.5f, 0.5f});
    EXPECT_EQ(lights.directional_count, 0) << "an ambient light turns the default rig off";
}

TEST(Lights, RunRenderPutsTheLightsAndCameraIntoTheView) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    const Entity camera = world.create();
    engine::Transform camera_transform;
    camera_transform.position = {1.0f, 2.0f, 10.0f};
    world.emplace<engine::Transform>(camera, camera_transform);
    world.emplace<engine::Camera>(camera);
    world.ctx<engine::ActiveCamera>().entity = camera;
    engine::ui::presentation_of(world).sizes.sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    world.ctx<engine::BoundWindows>().ids = {engine::kPrimaryWindow};

    const Entity cube = world.create();
    world.emplace<engine::Transform>(cube);
    world.emplace<engine::render::Renderable>(cube, engine::render::Renderable{
            std::make_shared<FakeMesh>(), std::make_shared<FakeMaterial>()});
    spawn_directional(world, turned(glm::half_pi<float>(), {0.0f, 1.0f, 0.0f}));
    world.emplace<AmbientLight>(world.create());

    world.run(engine::ecs::Schedule::Frame);

    ASSERT_EQ(commands.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdSetView>(commands[0]));
    const auto& view = std::get<engine::render::CmdSetView>(commands[0]);
    expect_near(view.camera_position, {1.0f, 2.0f, 10.0f});
    EXPECT_EQ(view.lights, engine::render::collect_scene_lights(world));
    ASSERT_EQ(view.lights.directional_count, 1);
    expect_near(view.lights.directions[0], {-1.0f, 0.0f, 0.0f});
    expect_near(view.lights.ambient, glm::vec3{0.2f});
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(commands[1]));
}

// Without anything to draw the buffer stays empty, as before views existed: no CmdSetView and no camera check.
TEST(Lights, RunRenderWithoutDrawsPushesNoView) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    world.ctx<engine::BoundWindows>().ids = {engine::kPrimaryWindow};
    world.emplace<DirectionalLight>(world.create());

    world.run(engine::ecs::Schedule::Frame);

    EXPECT_TRUE(commands.empty());
}
