#include <gtest/gtest.h>

#include <engine/core/bound_windows.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_serializer.h>
#include <engine/render/command_buffer.h>
#include <engine/render/commands.h>
#include <engine/render/fog.h>
#include <engine/render/material.h>
#include <engine/render/renderable.h>
#include <engine/ui/presentation.h>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <variant>

namespace {

using engine::ecs::Entity;
using engine::render::Fog;

constexpr engine::WindowId kSecondWindow{7};

class FakeMesh final : public engine::render::IMesh {
protected:
    bool replace(const engine::render::MeshDesc&) override {
        return true;
    }
};

class FakeMaterial final : public engine::render::IMaterial {
public:
    std::shared_ptr<engine::render::IShader> shader() const override { return {}; }
    std::shared_ptr<engine::render::ITexture> texture(int) const override { return {}; }
    glm::vec4 color() const override { return glm::vec4{1.0f}; }
    engine::render::RenderState render_state() const override { return {}; }
};

// A camera and one cube, drawn to the primary window and a second one, each with its own buffer.
struct FogScene {
    engine::render::CommandBuffer primary;
    engine::render::CommandBuffer second;
    engine::ecs::World world;
    Entity camera{};

    FogScene() {
        engine::register_engine_systems(world, engine::EngineSystemDeps{
                .commands_for_window = [this](engine::WindowId id) -> engine::render::CommandBuffer* {
                    if (id == engine::kPrimaryWindow) {
                        return &primary;
                    }
                    return id == kSecondWindow ? &second : nullptr;
                },
        });
        camera = world.create();
        world.emplace<engine::Transform>(camera);
        world.emplace<engine::Camera>(camera);
        world.ctx<engine::ActiveCamera>().entity = camera;
        auto& sizes = engine::ui::presentation_of(world).sizes.sizes;
        sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
        sizes[kSecondWindow] = engine::ui::WindowSize{320, 240};
        world.ctx<engine::BoundWindows>().ids = {engine::kPrimaryWindow, kSecondWindow};

        const Entity cube = world.create();
        world.emplace<engine::Transform>(cube);
        world.emplace<engine::render::Renderable>(cube, engine::render::Renderable{
                std::make_shared<FakeMesh>(), std::make_shared<FakeMaterial>()});
    }
};

const engine::render::CmdSetView& view_of(const engine::render::CommandBuffer& commands) {
    EXPECT_FALSE(commands.empty());
    return std::get<engine::render::CmdSetView>(commands[0]);
}

}

TEST(Fog, DefaultSetViewHasNoFog) {
    const engine::render::CmdSetView view;
    EXPECT_FALSE(view.fog.has_value());
}

TEST(Fog, ComponentDefaults) {
    const Fog fog;
    EXPECT_EQ(fog.color, glm::vec3(1.0f));
    EXPECT_FLOAT_EQ(fog.start, 0.0f);
    EXPECT_FLOAT_EQ(fog.end, 100.0f);
}

TEST(Fog, RunRenderWithoutFogComponentHasNoFog) {
    FogScene scene;
    scene.world.run(engine::ecs::Schedule::Frame);

    EXPECT_FALSE(view_of(scene.primary).fog.has_value());
    EXPECT_FALSE(view_of(scene.second).fog.has_value());
}

TEST(Fog, FogOnTheActiveCameraReachesEveryView) {
    FogScene scene;
    const Fog fog{.color = {0.6f, 0.7f, 0.9f}, .start = 32.0f, .end = 96.0f};
    scene.world.emplace<Fog>(scene.camera, fog);

    scene.world.run(engine::ecs::Schedule::Frame);

    EXPECT_EQ(view_of(scene.primary).fog, std::optional<Fog>(fog));
    EXPECT_EQ(view_of(scene.second).fog, std::optional<Fog>(fog));
}

// Fog belongs to the camera that renders, not to any entity in the world.
TEST(Fog, FogOnAnotherEntityIsIgnored) {
    FogScene scene;
    scene.world.emplace<Fog>(scene.world.create(), Fog{});

    scene.world.run(engine::ecs::Schedule::Frame);

    EXPECT_FALSE(view_of(scene.primary).fog.has_value());
}

// The editor's viewport renders with its own camera, unfogged, even when the scene's camera has fog.
TEST(Fog, WorldViewportPushesNoFog) {
    FogScene scene;
    scene.world.emplace<Fog>(scene.camera, Fog{});
    engine::render::CommandBuffer commands;

    engine::render_world_viewport(scene.world, engine::WorldViewportRender{
            .commands = &commands,
            .viewport = {0.0f, 0.0f, 160.0f, 90.0f},
    });

    EXPECT_FALSE(view_of(commands).fog.has_value());
}

TEST(Fog, SerializerRoundTrip) {
    constexpr std::string_view kToml = R"(
version = 1
active_camera = "cam"

[[entities]]
id = "cam"
transform = { position = [0.0, 0.0, 10.0] }
camera = { projection = "perspective", near_clip = 0.1, far_clip = 500.0 }
fog = { color = [0.5, 0.6, 0.7], start = 40.0, end = 120.0 }
)";
    engine::ecs::World world;
    auto loaded = engine::ecs::load_world_from_toml(world, kToml);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    const Entity cam = loaded->by_id.at("cam");
    const Fog expected{.color = {0.5f, 0.6f, 0.7f}, .start = 40.0f, .end = 120.0f};
    ASSERT_NE(world.try_get<Fog>(cam), nullptr);
    EXPECT_EQ(*world.try_get<Fog>(cam), expected);

    const std::string saved = engine::ecs::save_world_to_toml(world);
    engine::ecs::World reloaded_world;
    auto reloaded = engine::ecs::load_world_from_toml(reloaded_world, saved);
    ASSERT_TRUE(reloaded.has_value()) << reloaded.error().message;
    const Fog* fog = reloaded_world.try_get<Fog>(reloaded->by_id.at("cam"));
    ASSERT_NE(fog, nullptr);
    EXPECT_EQ(*fog, expected);
}
