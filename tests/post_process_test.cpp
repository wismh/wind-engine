#include <gtest/gtest.h>

#include <engine/builtin_ids.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/default_fatal_error.h>
#include <engine/core/bound_windows.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_serializer.h>
#include <engine/render/command_buffer.h>
#include <engine/render/commands.h>
#include <engine/render/material.h>
#include <engine/render/post_process.h>
#include <engine/render/primitives.h>
#include <engine/render/renderable.h>
#include <engine/ui/presentation.h>

#include <glm/vec4.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace {

using engine::ecs::Entity;
using engine::render::FrameTarget;
using engine::render::PostEffect;
using engine::render::PostImage;
using engine::render::PostStack;
using engine::render::SceneColorFormat;

constexpr engine::WindowId kSecondWindow{7};

class FakeMesh final : public engine::render::IMesh {
public:
    FakeMesh() : engine::render::IMesh(engine::render::make_quad()) {}

protected:
    bool replace(const engine::render::MeshDesc&) override { return true; }
};

class FakeMaterial final : public engine::render::IMaterial {
public:
    std::shared_ptr<engine::render::IShader> shader() const override { return {}; }
    std::shared_ptr<engine::render::ITexture> texture(int) const override { return {}; }
    glm::vec4 color() const override { return glm::vec4{1.0f}; }
    engine::render::RenderState render_state() const override { return {}; }
};

struct Scene {
    engine::render::CommandBuffer primary;
    engine::render::CommandBuffer second;
    engine::ecs::World world;
    Entity camera{};

    Scene() {
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
    }

    Entity cube() {
        const Entity entity = world.create();
        world.emplace<engine::Transform>(entity);
        world.emplace<engine::render::Renderable>(entity, engine::render::Renderable{
                std::make_shared<FakeMesh>(), std::make_shared<FakeMaterial>()});
        return entity;
    }
};

} // namespace

TEST(PostProcess, AbsentStackIsInactive) {
    const auto prepared = engine::render::prepare_post_stack(nullptr);
    ASSERT_TRUE(prepared.has_value());
    EXPECT_FALSE(prepared->hdr);
    EXPECT_TRUE(prepared->effects.empty());

    const PostStack empty;
    EXPECT_FALSE(empty.active());
    const auto prepared_empty = engine::render::prepare_post_stack(&empty);
    ASSERT_TRUE(prepared_empty.has_value());
    EXPECT_FALSE(prepared_empty->hdr);
    EXPECT_TRUE(prepared_empty->effects.empty());
}

TEST(PostProcess, HdrWithNoEffectsStaysActive) {
    const PostStack stack{.hdr = true};
    EXPECT_TRUE(stack.active());
    const auto prepared = engine::render::prepare_post_stack(&stack);
    ASSERT_TRUE(prepared.has_value());
    EXPECT_TRUE(prepared->hdr);
    EXPECT_TRUE(prepared->effects.empty());
}

TEST(PostProcess, DepthBuiltinForcesSampleDepth) {
    const PostStack stack{.effects = {PostEffect{
            .shader = engine::builtin::shader_post_depth,
            .params = {0.2f, 0.4f, 0.6f, 1.0f},
            .sample_depth = false,
    }}};
    const auto prepared = engine::render::prepare_post_stack(&stack);
    ASSERT_TRUE(prepared.has_value());
    ASSERT_EQ(prepared->effects.size(), 1u);
    EXPECT_TRUE(prepared->effects[0].sample_depth);
    EXPECT_EQ(prepared->effects[0].params, glm::vec4(0.2f, 0.4f, 0.6f, 1.0f));
}

TEST(PostProcess, GradeDoesNotForceDepth) {
    const PostStack stack{.effects = {PostEffect{.shader = engine::builtin::shader_post_grade}}};
    const auto prepared = engine::render::prepare_post_stack(&stack);
    ASSERT_TRUE(prepared.has_value());
    ASSERT_EQ(prepared->effects.size(), 1u);
    EXPECT_FALSE(prepared->effects[0].sample_depth);
}

TEST(PostProcess, BloomWithoutHdrIsAnError) {
    const PostStack stack{.effects = {PostEffect{.shader = engine::builtin::shader_post_bloom}}};
    const auto prepared = engine::render::prepare_post_stack(&stack);
    ASSERT_FALSE(prepared.has_value());
    EXPECT_NE(prepared.error().find("Bloom"), std::string::npos);
}

TEST(PostProcess, StepsPingPongAndTonemapLandsOnTheWindow) {
    const auto one = engine::render::post_steps(false, 1);
    ASSERT_EQ(one.size(), 1u);
    EXPECT_EQ(one[0].effect, 0);
    EXPECT_FALSE(one[0].tonemap);
    EXPECT_EQ(one[0].source, PostImage::SceneA);
    EXPECT_EQ(one[0].dest, PostImage::Window);

    const auto two = engine::render::post_steps(false, 2);
    ASSERT_EQ(two.size(), 2u);
    EXPECT_EQ(two[0].dest, PostImage::SceneB);
    EXPECT_EQ(two[1].source, PostImage::SceneB);
    EXPECT_EQ(two[1].dest, PostImage::Window);

    const auto hdr_only = engine::render::post_steps(true, 0);
    ASSERT_EQ(hdr_only.size(), 1u);
    EXPECT_TRUE(hdr_only[0].tonemap);
    EXPECT_EQ(hdr_only[0].source, PostImage::SceneA);
    EXPECT_EQ(hdr_only[0].dest, PostImage::Window);

    const auto hdr_two = engine::render::post_steps(true, 2);
    ASSERT_EQ(hdr_two.size(), 3u);
    EXPECT_EQ(hdr_two[0].dest, PostImage::SceneB);
    EXPECT_EQ(hdr_two[1].source, PostImage::SceneB);
    EXPECT_EQ(hdr_two[1].dest, PostImage::SceneA);
    EXPECT_TRUE(hdr_two[2].tonemap);
    EXPECT_EQ(hdr_two[2].source, PostImage::SceneA);
    EXPECT_EQ(hdr_two[2].dest, PostImage::Window);
}

TEST(PostProcess, SceneColorFormat) {
    EXPECT_EQ(engine::render::scene_color_format(false, false, true), SceneColorFormat::Rgba8);
    EXPECT_EQ(engine::render::scene_color_format(true, false, false), SceneColorFormat::Rgba8);
    EXPECT_EQ(engine::render::scene_color_format(true, false, true), SceneColorFormat::R11G11B10);
    EXPECT_EQ(engine::render::scene_color_format(true, true, true), SceneColorFormat::Rgba16F);
}

TEST(PostProcess, FrameTargets) {
    engine::render::CommandBuffer commands;
    engine::render::CmdSetView view;
    view.post.hdr = true;
    commands.push(view);
    commands.push(engine::render::CmdDrawMesh{});
    commands.push(engine::render::CmdPostProcess{});
    commands.push(engine::render::CmdDrawUI{});

    const auto targets = engine::render::frame_targets(commands);
    ASSERT_EQ(targets.size(), 4u);
    EXPECT_EQ(targets[0], FrameTarget::Window);
    EXPECT_EQ(targets[1], FrameTarget::Scene);
    EXPECT_EQ(targets[2], FrameTarget::Window);
    EXPECT_EQ(targets[3], FrameTarget::Window);

    engine::render::CommandBuffer viewport;
    engine::render::CmdSetView cropped = view;
    cropped.viewport = engine::render::Rect{0.0f, 0.0f, 10.0f, 10.0f};
    viewport.push(cropped);
    viewport.push(engine::render::CmdDrawMesh{});
    const auto cropped_targets = engine::render::frame_targets(viewport);
    EXPECT_EQ(cropped_targets[0], FrameTarget::Window);
    EXPECT_EQ(cropped_targets[1], FrameTarget::Window);
}

TEST(PostProcess, RunRenderWithoutAStackPushesNoPost) {
    Scene scene;
    scene.cube();
    scene.world.run(engine::ecs::Schedule::Frame);

    ASSERT_FALSE(scene.primary.empty());
    const auto& view = std::get<engine::render::CmdSetView>(scene.primary[0]);
    EXPECT_FALSE(view.post.active());
    for (const engine::render::Command& command : scene.primary) {
        EXPECT_FALSE(std::holds_alternative<engine::render::CmdPostProcess>(command));
    }
    EXPECT_FALSE(std::get<engine::render::CmdSetView>(scene.second[0]).post.active());
}

TEST(PostProcess, RunRenderWithNothingToDrawAndNoStackStaysEmpty) {
    Scene scene;
    scene.world.run(engine::ecs::Schedule::Frame);
    EXPECT_TRUE(scene.primary.empty());
    EXPECT_TRUE(scene.second.empty());
}

TEST(PostProcess, StackOnAnotherEntityIsIgnored) {
    Scene scene;
    scene.cube();
    scene.world.emplace<PostStack>(scene.world.create(), PostStack{.hdr = true});
    scene.world.run(engine::ecs::Schedule::Frame);
    EXPECT_FALSE(std::get<engine::render::CmdSetView>(scene.primary[0]).post.active());
}

TEST(PostProcess, WorldViewportLeavesPostEmpty) {
    Scene scene;
    scene.cube();
    scene.world.emplace<PostStack>(scene.camera, PostStack{
            .effects = {PostEffect{.shader = engine::builtin::shader_post_grade}}});
    engine::render::CommandBuffer commands;
    engine::render_world_viewport(scene.world, engine::WorldViewportRender{
            .commands = &commands,
            .viewport = {0.0f, 0.0f, 160.0f, 90.0f},
    });
    ASSERT_FALSE(commands.empty());
    EXPECT_FALSE(std::get<engine::render::CmdSetView>(commands[0]).post.active());
    for (const engine::render::Command& command : commands) {
        EXPECT_FALSE(std::holds_alternative<engine::render::CmdPostProcess>(command));
    }
}

TEST(PostProcess, BloomWithoutHdrIsFatal) {
    Scene scene;
    scene.world.emplace<PostStack>(scene.camera, PostStack{
            .effects = {PostEffect{.shader = engine::builtin::shader_post_bloom}}});
    EXPECT_DEATH(scene.world.run(engine::ecs::Schedule::Frame), "Bloom post effect requires PostStack::hdr");
}

TEST(PostProcess, MissingShaderIsFatalEvenWithNoDraws) {
    engine::AssetsDb assets(engine::default_fatal_error());
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{
            .commands = &commands,
            .assets = &assets,
    });
    const Entity camera = world.create();
    world.emplace<engine::Transform>(camera);
    world.emplace<engine::Camera>(camera);
    world.ctx<engine::ActiveCamera>().entity = camera;
    world.ctx<engine::BoundWindows>().ids = {engine::kPrimaryWindow};
    world.emplace<PostStack>(camera, PostStack{
            .effects = {PostEffect{.shader = engine::builtin::shader_post_grade}}});
    EXPECT_DEATH(world.run(engine::ecs::Schedule::Frame), "did not load");
}

TEST(PostProcess, SerializerRoundTrip) {
    constexpr std::string_view kToml = R"(
version = 1
active_camera = "cam"

[[entities]]
id = "cam"
transform = { position = [0.0, 0.0, 10.0] }
camera = { projection = "perspective", near_clip = 0.1, far_clip = 500.0 }
post_stack = { hdr = true, effects = [
    { shader = "a0e1b2c3d4f5678901234567890abc10", params = [0.8, 1.1, 0.9, 1.0], sample_depth = false },
    { shader = "a0e1b2c3d4f5678901234567890abc13", params = [1.0, 1.0, 1.0, 1.0], sample_depth = true }
] }
)";
    engine::ecs::World world;
    auto loaded = engine::ecs::load_world_from_toml(world, kToml);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    const PostStack expected{
            .hdr = true,
            .effects = {
                    PostEffect{.shader = engine::builtin::shader_post_grade, .params = {0.8f, 1.1f, 0.9f, 1.0f}},
                    PostEffect{.shader = engine::builtin::shader_post_depth, .sample_depth = true},
            },
    };
    const PostStack* stack = world.try_get<PostStack>(loaded->by_id.at("cam"));
    ASSERT_NE(stack, nullptr);
    EXPECT_EQ(*stack, expected);

    const std::string saved = engine::ecs::save_world_to_toml(world);
    engine::ecs::World again;
    auto reloaded = engine::ecs::load_world_from_toml(again, saved);
    ASSERT_TRUE(reloaded.has_value()) << reloaded.error().message;
    const PostStack* round = again.try_get<PostStack>(reloaded->by_id.at("cam"));
    ASSERT_NE(round, nullptr);
    EXPECT_EQ(*round, expected);
}
