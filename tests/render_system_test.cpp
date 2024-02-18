#include <gtest/gtest.h>

#include <engine/audio/audio_system.h>
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
#include <engine/resources/assets_db.h>
#include <engine/resources/fatal_error.h>
#include <engine/resources/meta.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/view_model.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
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
        property(engine::ui::intern("title"), title);
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

glm::mat4 expected_trs(const engine::Transform& transform) {
    glm::mat4 model(1.0f);
    model = glm::translate(model, transform.position);
    model = glm::rotate(model, transform.rotation.x, glm::vec3{1.0f, 0.0f, 0.0f});
    model = glm::rotate(model, transform.rotation.y, glm::vec3{0.0f, 1.0f, 0.0f});
    model = glm::rotate(model, transform.rotation.z, glm::vec3{0.0f, 0.0f, 1.0f});
    model = glm::scale(model, transform.scale);
    return model;
}

void expect_mat4_eq(const glm::mat4& actual, const glm::mat4& expected) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_NEAR(actual[column][row], expected[column][row], 1e-4f)
                    << "at [" << column << "][" << row << "]";
        }
    }
}

bool mat4_eq(const glm::mat4& a, const glm::mat4& b, float epsilon) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (std::abs(a[column][row] - b[column][row]) > epsilon) {
                return false;
            }
        }
    }
    return true;
}

struct TempDir {
    std::filesystem::path path;

    TempDir() {
        static int seq = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
                ("wind_playsfx_" + std::to_string(stamp) + "_" + std::to_string(++seq));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

void write_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << text;
}

void write_ui_asset(const std::filesystem::path& xml_path, std::string_view xml, std::string_view guid) {
    write_file(xml_path, xml);
    std::filesystem::path meta = xml_path;
    meta += ".meta";
    write_file(meta, std::string("guid = \"") + std::string(guid) + "\"\nimporter = \"ui\"\n");
}

}

TEST(RenderSystem, SortThenPushMesh) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
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

    world.run(engine::ecs::Schedule::Frame);

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
        engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands, .fatal = &fatal});
        spawn_camera(world);

        const auto material = std::make_shared<FakeMaterial>();
        const engine::ecs::Entity entity = world.create();
        world.emplace<engine::Transform>(entity, engine::Transform{});
        world.emplace<engine::render::Renderable>(entity, make_renderable(nullptr, material, 0));

        EXPECT_THROW(world.run(engine::ecs::Schedule::Frame), std::runtime_error);
        EXPECT_EQ(fatal.call_count, 1);
        EXPECT_TRUE(commands.empty());
    }
    {
        engine::render::CommandBuffer commands;
        RecordingFatalError fatal;
        engine::ecs::World world;
        engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands, .fatal = &fatal});
        spawn_camera(world);

        const auto mesh = std::make_shared<FakeMesh>();
        const engine::ecs::Entity entity = world.create();
        world.emplace<engine::Transform>(entity, engine::Transform{});
        world.emplace<engine::render::Renderable>(entity, make_renderable(mesh, nullptr, 0));

        EXPECT_THROW(world.run(engine::ecs::Schedule::Frame), std::runtime_error);
        EXPECT_EQ(fatal.call_count, 1);
        EXPECT_TRUE(commands.empty());
    }
}

TEST(RenderSystem, UiCommandsAfterWorld) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
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

    world.run(engine::ecs::Schedule::Frame);

    ASSERT_EQ(commands.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(commands[0]));
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawUI>(commands[1]));
    EXPECT_EQ(std::get<engine::render::CmdDrawUI>(commands[1]).rect, canvas.rect);
}

TEST(RenderSystem, BindPhaseUpdatesInstance) {
    engine::ecs::World world;
    engine::register_engine_systems(world);

    auto vm = std::make_shared<TitleViewModel>();
    vm->title.set("Hello");
    const auto parsed =
            engine::ui::parse_xml(R"(<Canvas><Label text="{binding title}"/></Canvas>)", nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    engine::ui::UiCanvas canvas;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed});

    world.run(engine::ecs::Schedule::Frame);
    const engine::ui::Element* label =
            engine::ui::find_by_kind(world.get<engine::ui::UiInstance>(entity).document.root, engine::ui::ElementKind::Label);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text, "Hello");

    vm->title.set("World");
    world.run(engine::ecs::Schedule::Frame);
    EXPECT_EQ(engine::ui::find_by_kind(world.get<engine::ui::UiInstance>(entity).document.root, engine::ui::ElementKind::Label)
                      ->text,
            "World");
}

TEST(RenderSystem, BindPhaseClonesDocumentFromAssets) {
    constexpr std::string_view kHudGuid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1";
    constexpr std::string_view kOtherGuid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa2";

    TempDir tree;
    write_ui_asset(tree.path / "hud.xml", R"(<Canvas><Label text="{binding title}"/></Canvas>)", kHudGuid);
    write_ui_asset(tree.path / "other.xml", R"(<Canvas><Label text="Rebuilt"/></Canvas>)", kOtherGuid);

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kHudGuid}, "hud.xml", engine::ImporterKind::Ui});
    catalog.add({engine::AssetId{kOtherGuid}, "other.xml", engine::ImporterKind::Ui});
    write_file(tree.path / "catalog.toml", catalog.serialize());

    RecordingFatalError fatal;
    engine::AssetsDb db(fatal);
    const auto loaded = db.load_catalog(tree.path / "catalog.toml", tree.path);
    ASSERT_TRUE(loaded.has_value());

    auto vm = std::make_shared<TitleViewModel>();
    vm->title.set("Hello");

    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.fatal = &fatal, .assets = &db});

    engine::ui::UiCanvas canvas;
    canvas.document = engine::AssetId{kHudGuid};
    canvas.data_context = vm;
    canvas.fit = engine::ui::UiFit::Fixed;
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);

    world.run(engine::ecs::Schedule::Frame);
    const engine::ui::UiInstance* instance = world.try_get<engine::ui::UiInstance>(entity);
    ASSERT_NE(instance, nullptr);
    const engine::ui::Element* label =
            engine::ui::find_by_kind(instance->document.root, engine::ui::ElementKind::Label);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text, "Hello");

    world.get<engine::ui::UiCanvas>(entity).document = engine::AssetId{kOtherGuid};
    world.run(engine::ecs::Schedule::Frame);
    instance = world.try_get<engine::ui::UiInstance>(entity);
    ASSERT_NE(instance, nullptr);
    label = engine::ui::find_by_kind(instance->document.root, engine::ui::ElementKind::Label);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text, "Rebuilt");
}

TEST(RenderSystem, BindPhaseSkipsCloneWhenAssetsNull) {
    auto vm = std::make_shared<TitleViewModel>();
    vm->title.set("Hello");

    engine::ecs::World world;
    engine::register_engine_systems(world);

    engine::ui::UiCanvas canvas;
    canvas.document = engine::AssetId{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1"};
    canvas.data_context = vm;
    canvas.fit = engine::ui::UiFit::Fixed;
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);

    world.run(engine::ecs::Schedule::Frame);
    EXPECT_EQ(world.try_get<engine::ui::UiInstance>(entity), nullptr);
}

TEST(RenderSystem, ModelMatrixIsTranslateRotateScale) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_camera(world);

    const auto mesh = std::make_shared<FakeMesh>();
    const auto material = std::make_shared<FakeMaterial>();
    engine::Transform transform;
    transform.position = {1.0f, 2.0f, 3.0f};
    transform.rotation = {0.1f, 0.2f, 0.3f};
    transform.scale = {2.0f, 3.0f, 4.0f};

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, transform);
    world.emplace<engine::render::Renderable>(entity, make_renderable(mesh, material, 0));

    world.run(engine::ecs::Schedule::Frame);

    ASSERT_EQ(commands.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(commands[0]));
    const glm::mat4 model = std::get<engine::render::CmdDrawMesh>(commands[0]).model;
    expect_mat4_eq(model, expected_trs(transform));
    EXPECT_FALSE(mat4_eq(model, glm::translate(glm::mat4(1.0f), transform.position), 1e-4f));
}

TEST(Audio, PlaySfxEventType) {
    engine::ecs::World world;
    engine::register_engine_systems(world);
    engine::ecs::EventWriter<engine::PlaySfxEvent>{world}.send(engine::PlaySfxEvent{
            .id = engine::AssetId{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
            .volume_scale = 0.5f,
    });
    engine::ecs::EventWriter<engine::PlayMusicEvent>{world}.send(engine::PlayMusicEvent{
            .id = engine::AssetId{"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},
    });
    world.run(engine::ecs::Schedule::Frame);
    SUCCEED();
}

TEST(Audio, PlaySfxGetWhenDepsSet) {
    TempDir dir;
    std::filesystem::create_directories(dir.path / "sfx");
    {
        std::ofstream out(dir.path / "sfx" / "step.wav", std::ios::trunc);
        ASSERT_TRUE(out.is_open());
    }

    RecordingFatalError fatal;
    engine::AssetsDb db(fatal);
    db.set_root(dir.path);
    const engine::AssetId id{"b1c2d3e4f567890123456789012345ab"};
    engine::CookedCatalog catalog;
    catalog.add({id, "sfx/step.wav", engine::ImporterKind::Audio});
    db.set_catalog(std::move(catalog));

    engine::AudioSystem audio;
    ASSERT_TRUE(audio.init());

    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.assets = &db, .audio = &audio});
    engine::ecs::EventWriter<engine::PlaySfxEvent>{world}.send(engine::PlaySfxEvent{
            .id = id,
            .volume_scale = 0.5f,
    });
    engine::ecs::EventWriter<engine::PlayMusicEvent>{world}.send(engine::PlayMusicEvent{
            .id = id,
            .loop = true,
            .fade_seconds = 0.f,
    });
    world.run(engine::ecs::Schedule::Frame);

    EXPECT_EQ(audio.sfx_play_count(), 1);
    EXPECT_TRUE(audio.is_music_playing());
}

TEST(Audio, PlaySfxMissingCueIsFatal) {
    RecordingFatalError fatal;
    engine::AssetsDb db(fatal);
    engine::AudioSystem audio;
    ASSERT_TRUE(audio.init());

    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.assets = &db, .audio = &audio});
    engine::ecs::EventWriter<engine::PlaySfxEvent>{world}.send(engine::PlaySfxEvent{
            .id = engine::AssetId{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
            .volume_scale = 0.5f,
    });
    EXPECT_THROW(world.run(engine::ecs::Schedule::Frame), std::runtime_error);
    EXPECT_EQ(fatal.call_count, 1);
}
