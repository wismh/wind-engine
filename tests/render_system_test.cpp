#include <gtest/gtest.h>

#include <engine/audio/events.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/events.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/render/command_buffer.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>
#include <engine/render/renderable.h>
#include <engine/resources/fatal_error.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/view_model.h>

#include <glm/vec4.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

class FakeMesh final : public engine::render::IMesh {};

class FakeMaterial final : public engine::render::IMaterial {
public:
    std::shared_ptr<engine::render::IShader> Shader() const override {
        return {};
    }

    std::shared_ptr<engine::render::ITexture> Texture(int) const override {
        return {};
    }

    glm::vec4 Color() const override {
        return {1.0f, 1.0f, 1.0f, 1.0f};
    }

    engine::render::BlendMode Blend() const override {
        return engine::render::BlendMode::Opaque;
    }
};

class RecordingFatalError final : public engine::IFatalError {
public:
    int call_count = 0;
    std::string last_message;

    void report(std::string_view message) override {
        ++call_count;
        last_message = std::string(message);
        throw std::runtime_error(last_message);
    }
};

class TitleViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<std::string> title;

    TitleViewModel() {
        Property("Title", title);
    }
};

void spawn_camera(engine::ecs::World& world) {
    const engine::ecs::Entity camera = world.create();
    world.emplace<engine::Transform>(camera, engine::Transform{});
    world.emplace<engine::Camera>(camera, engine::Camera{});
    world.ctx<engine::ActiveCamera>().entity = camera;
    world.ctx<engine::ui::WindowSize>().width = 800;
    world.ctx<engine::ui::WindowSize>().height = 600;
}

engine::render::Renderable make_renderable(std::shared_ptr<engine::render::IMesh> mesh,
        std::shared_ptr<engine::render::IMaterial> material, int layer) {
    return engine::render::Renderable{
            std::move(mesh),
            std::move(material),
            glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
            layer,
            0,
    };
}

}

TEST(RenderSystem, SortThenPushMesh) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::RegisterEngineSystems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_camera(world);

    const auto mesh_high = std::make_shared<FakeMesh>();
    const auto mesh_low = std::make_shared<FakeMesh>();
    const auto material = std::make_shared<FakeMaterial>();

    const engine::ecs::Entity high = world.create();
    world.emplace<engine::Transform>(high, engine::Transform{});
    world.emplace<engine::render::Renderable>(high, make_renderable(mesh_high, material, 10));

    const engine::ecs::Entity low = world.create();
    world.emplace<engine::Transform>(low, engine::Transform{});
    world.emplace<engine::render::Renderable>(low, make_renderable(mesh_low, material, 0));

    world.Run(engine::ecs::Schedule::Frame);

    ASSERT_EQ(commands.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(commands[0]));
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(commands[1]));
    EXPECT_EQ(std::get<engine::render::CmdDrawMesh>(commands[0]).mesh, mesh_low);
    EXPECT_EQ(std::get<engine::render::CmdDrawMesh>(commands[1]).mesh, mesh_high);
}

TEST(RenderSystem, MissingMeshOrMaterialIsFatal) {
    {
        engine::render::CommandBuffer commands;
        RecordingFatalError fatal;
        engine::ecs::World world;
        engine::RegisterEngineSystems(world, engine::EngineSystemDeps{.commands = &commands, .fatal = &fatal});
        spawn_camera(world);

        const auto material = std::make_shared<FakeMaterial>();
        const engine::ecs::Entity entity = world.create();
        world.emplace<engine::Transform>(entity, engine::Transform{});
        world.emplace<engine::render::Renderable>(entity, make_renderable(nullptr, material, 0));

        EXPECT_THROW(world.Run(engine::ecs::Schedule::Frame), std::runtime_error);
        EXPECT_EQ(fatal.call_count, 1);
        EXPECT_TRUE(commands.empty());
    }
    {
        engine::render::CommandBuffer commands;
        RecordingFatalError fatal;
        engine::ecs::World world;
        engine::RegisterEngineSystems(world, engine::EngineSystemDeps{.commands = &commands, .fatal = &fatal});
        spawn_camera(world);

        const auto mesh = std::make_shared<FakeMesh>();
        const engine::ecs::Entity entity = world.create();
        world.emplace<engine::Transform>(entity, engine::Transform{});
        world.emplace<engine::render::Renderable>(entity, make_renderable(mesh, nullptr, 0));

        EXPECT_THROW(world.Run(engine::ecs::Schedule::Frame), std::runtime_error);
        EXPECT_EQ(fatal.call_count, 1);
        EXPECT_TRUE(commands.empty());
    }
}

TEST(RenderSystem, UiCommandsAfterWorld) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::RegisterEngineSystems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_camera(world);

    const auto mesh = std::make_shared<FakeMesh>();
    const auto material = std::make_shared<FakeMaterial>();
    const engine::ecs::Entity mesh_entity = world.create();
    world.emplace<engine::Transform>(mesh_entity, engine::Transform{});
    world.emplace<engine::render::Renderable>(mesh_entity, make_renderable(mesh, material, 0));

    engine::ui::UiCanvas canvas;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.rect = engine::render::Rect{8.0f, 16.0f, 32.0f, 48.0f};
    const engine::ecs::Entity hud = world.create();
    world.emplace<engine::ui::UiCanvas>(hud, canvas);

    world.Run(engine::ecs::Schedule::Frame);

    ASSERT_EQ(commands.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(commands[0]));
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawUI>(commands[1]));
    EXPECT_EQ(std::get<engine::render::CmdDrawUI>(commands[1]).rect, canvas.rect);
}

TEST(RenderSystem, BindPhaseUpdatesInstance) {
    engine::ecs::World world;
    engine::RegisterEngineSystems(world);

    auto vm = std::make_shared<TitleViewModel>();
    vm->title.Set("Hello");
    const auto parsed =
            engine::ui::parse_xml(R"(<Canvas><Label Text="{Binding Title}"/></Canvas>)", nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    engine::ui::UiCanvas canvas;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed});

    world.Run(engine::ecs::Schedule::Frame);
    const engine::ui::Element* label =
            engine::ui::find_by_kind(world.get<engine::ui::UiInstance>(entity).document.root, engine::ui::ElementKind::Label);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text, "Hello");

    vm->title.Set("World");
    world.Run(engine::ecs::Schedule::Frame);
    EXPECT_EQ(engine::ui::find_by_kind(world.get<engine::ui::UiInstance>(entity).document.root, engine::ui::ElementKind::Label)
                      ->text,
            "World");
}

TEST(Audio, PlaySfxEventType) {
    engine::ecs::World world;
    engine::RegisterEngineSystems(world);
    engine::ecs::EventWriter<engine::PlaySfxEvent>{world}.send(engine::PlaySfxEvent{
            .id = engine::AssetId{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
            .volume_scale = 0.5f,
    });
    engine::ecs::EventWriter<engine::PlayMusicEvent>{world}.send(engine::PlayMusicEvent{
            .id = engine::AssetId{"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},
    });
    world.Run(engine::ecs::Schedule::Frame);
    SUCCEED();
}
