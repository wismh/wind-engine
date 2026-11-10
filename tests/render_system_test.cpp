#include <gtest/gtest.h>

#include <engine/audio/audio_system.h>
#include <engine/audio/events.h>
#include <engine/core/input_system.h>
#include <engine/core/time.h>
#include <engine/log.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/events.h>
#include <engine/ecs/fluid_2d.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_serializer.h>
#include <engine/render/command_buffer.h>
#include <engine/render/fluid_render_2d.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>
#include <engine/render/particles.h>
#include <engine/render/primitives.h>
#include <engine/render/renderable.h>
#include <engine/render/sprite.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/fatal_error.h>
#include <engine/resources/meta.h>
#include <engine/ui/canvas.h>
#include <engine/core/bound_windows.h>
#include <engine/ui/presentation.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include "ui/painter.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include "fixtures/fatal_fakes.h"

namespace {

class FakeMesh final : public engine::render::IMesh {
public:
    FakeMesh() : engine::render::IMesh(engine::render::make_quad()) {}
    explicit FakeMesh(const engine::render::MeshDesc& desc) : engine::render::IMesh(desc) {}

protected:
    bool replace(const engine::render::MeshDesc&) override {
        return true;
    }
};
class FakeTexture final : public engine::render::ITexture {};

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

    engine::render::RenderState render_state() const override {
        return {.blend = engine::render::BlendMode::Opaque};
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
    engine::ui::presentation_of(world).sizes.sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    world.ctx<engine::BoundWindows>().ids = {engine::kPrimaryWindow};
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
    model *= glm::mat4_cast(transform.rotation);
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

void write_css_asset(const std::filesystem::path& css_path, std::string_view css, std::string_view guid) {
    write_file(css_path, css);
    std::filesystem::path meta = css_path;
    meta += ".meta";
    write_file(meta, std::string("guid = \"") + std::string(guid) + "\"\nimporter = \"css\"\n");
}

class FillPainter final : public engine::ui::IUiPainter {
public:
    glm::vec4 last_fill{};
    bool had_fill = false;

    void save() override {}
    void restore() override {}
    void scissor(const engine::render::Rect&) override {}
    void apply_transform(glm::vec2, float, float) override {}
    void apply_view(glm::vec2, glm::vec2, float) override {}
    void set_opacity(float) override {}
    void fill_rounded_rect(const engine::render::Rect&, float, glm::vec4 color) override {
        last_fill = color;
        had_fill = true;
    }
    void fill_rounded_rect_gradient(const engine::render::Rect&, float, const engine::ui::Gradient&) override {}
    void stroke_rounded_rect(const engine::render::Rect&, float, float, glm::vec4) override {}
    void draw_line(glm::vec2, glm::vec2, glm::vec4, float) override {}
    void stroke_arc(glm::vec2, float, float, float, float, glm::vec4) override {}
    void fill_path(std::span<const engine::ui::PathSegment>, glm::vec4) override {}
    void stroke_path(std::span<const engine::ui::PathSegment>, float, glm::vec4) override {}
    void set_font(engine::AssetId, float) override {}
    void fill_text(std::string_view, glm::vec2, glm::vec4, engine::ui::UiAlign, engine::ui::UiAlign) override {}
    void image(engine::AssetId, const engine::render::Rect&) override {}
    void image_repeat(engine::AssetId, const engine::render::Rect&) override {}
    void image_nine_slice(engine::AssetId, const engine::render::Rect&, const engine::ui::BoxInsets&) override {}
    glm::vec2 measure_text(std::string_view text, engine::AssetId, float size) override {
        return {static_cast<float>(text.size()) * size * 0.5f, size};
    }
};

glm::vec4 paint_button_fill(engine::ui::UiInstance& instance) {
    FillPainter painter;
    const engine::ui::Stylesheet* sheet = instance.stylesheet ? &*instance.stylesheet : nullptr;
    engine::ui::paint_document(instance.document, sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 80.f, 40.f}});
    EXPECT_TRUE(painter.had_fill);
    return painter.last_fill;
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

    ASSERT_EQ(commands.size(), 3u);
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdSetView>(commands[0]));
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(commands[1]));
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(commands[2]));
    EXPECT_EQ(std::get<engine::render::CmdDrawMesh>(commands[1]).mesh, mesh_low);
    EXPECT_EQ(std::get<engine::render::CmdDrawMesh>(commands[2]).mesh, mesh_high);
}

TEST(RenderSystem, MaterialOverridePropagatesToCommand) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_camera(world);

    const auto mesh = std::make_shared<FakeMesh>();
    const auto material = std::make_shared<FakeMaterial>();
    const auto override_texture = std::make_shared<FakeTexture>();

    engine::render::MaterialOverride override;
    override.albedo = override_texture;
    override.set_vec4("uFlashAmount", glm::vec4{1.0f, 1.0f, 1.0f, 0.75f});

    engine::render::Renderable renderable = make_renderable(mesh, material, 0);
    renderable.material_override = override;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{});
    world.emplace<engine::render::Renderable>(entity, renderable);

    world.run(engine::ecs::Schedule::Frame);

    ASSERT_EQ(commands.size(), 2u);
    const auto* cmd = std::get_if<engine::render::CmdDrawMesh>(&commands[1]);
    ASSERT_NE(cmd, nullptr);
    ASSERT_TRUE(cmd->material_override.has_value());
    EXPECT_EQ(cmd->material_override->albedo, override_texture);
    ASSERT_EQ(cmd->material_override->vec4_params.size(), 1u);
    EXPECT_EQ(cmd->material_override->vec4_params[0].first, "uFlashAmount");
    EXPECT_EQ(cmd->material_override->vec4_params[0].second, (glm::vec4{1.0f, 1.0f, 1.0f, 0.75f}));
}

TEST(RenderSystem, MissingMeshOrMaterialIsFatal) {
    {
        engine::render::CommandBuffer commands;
        fakes::RecordingFatal fatal;
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
        fakes::RecordingFatal fatal;
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

TEST(RenderSystem, SceneWithoutValidActiveCameraIsFatal) {
    engine::render::CommandBuffer commands;
    fakes::RecordingFatal fatal;
    engine::ecs::World world;
    engine::register_engine_systems(world,
            engine::EngineSystemDeps{.commands = &commands, .fatal = &fatal});
    world.ctx<engine::BoundWindows>().ids = {engine::kPrimaryWindow};

    const auto mesh = std::make_shared<FakeMesh>();
    const auto material = std::make_shared<FakeMaterial>();
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{});
    world.emplace<engine::render::Renderable>(entity, make_renderable(mesh, material, 0));

    EXPECT_THROW(world.run(engine::ecs::Schedule::Frame), fakes::FatalReport);
    EXPECT_EQ(fatal.call_count, 1);
}

TEST(RenderSystem, SpriteWithoutValidActiveCameraIsFatal) {
    engine::render::CommandBuffer commands;
    fakes::RecordingFatal fatal;
    engine::ecs::World world;
    engine::register_engine_systems(world,
            engine::EngineSystemDeps{.commands = &commands, .fatal = &fatal});
    world.ctx<engine::BoundWindows>().ids = {engine::kPrimaryWindow};

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{});
    world.emplace<engine::render::Sprite>(entity, engine::render::Sprite{});

    EXPECT_THROW(world.run(engine::ecs::Schedule::Frame), fakes::FatalReport);
    EXPECT_EQ(fatal.call_count, 1);
}

TEST(RenderSystem, UiOnlyWorldNeedsNoCamera) {
    engine::render::CommandBuffer commands;
    fakes::RecordingFatal fatal;
    engine::ecs::World world;
    engine::register_engine_systems(world,
            engine::EngineSystemDeps{.commands = &commands, .fatal = &fatal});
    world.ctx<engine::BoundWindows>().ids = {engine::kPrimaryWindow};

    engine::ui::UiCanvas canvas;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.rect = engine::render::Rect{8.0f, 16.0f, 32.0f, 48.0f};
    world.emplace<engine::ui::UiCanvas>(world.create(), canvas);

    world.run(engine::ecs::Schedule::Frame);

    EXPECT_EQ(fatal.call_count, 0);
    ASSERT_EQ(commands.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawUI>(commands[0]));
}

TEST(RenderSystem, SceneWithoutCommandBuffersRegistersNoRenderSystem) {
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{});
    world.ctx<engine::BoundWindows>().ids = {engine::kPrimaryWindow};

    const auto mesh = std::make_shared<FakeMesh>();
    const auto material = std::make_shared<FakeMaterial>();
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{});
    world.emplace<engine::render::Renderable>(entity, make_renderable(mesh, material, 0));

    // A headless world has nothing to draw into, so even a scene with no camera is not a broken world.
    EXPECT_NO_THROW(world.run(engine::ecs::Schedule::Frame));
}

TEST(RenderSystem, SkipsScenePassWhenNoRenderables) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_camera(world);

    engine::ui::UiCanvas canvas;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.rect = engine::render::Rect{8.0f, 16.0f, 32.0f, 48.0f};
    const engine::ecs::Entity hud = world.create();
    world.emplace<engine::ui::UiCanvas>(hud, canvas);

    world.run(engine::ecs::Schedule::Frame);

    ASSERT_EQ(commands.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawUI>(commands[0]));
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

    ASSERT_EQ(commands.size(), 3u);
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdSetView>(commands[0]));
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(commands[1]));
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawUI>(commands[2]));
    EXPECT_EQ(std::get<engine::render::CmdDrawUI>(commands[2]).rect, canvas.rect);
}

TEST(RenderSystem, UiCanvasForSecondaryWindowRoutesThroughCommandsForWindow) {
    const engine::WindowId secondary{3};
    engine::render::CommandBuffer primary_commands;
    engine::render::CommandBuffer secondary_commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{
            .commands = &primary_commands,
            .commands_for_window = [&](engine::WindowId id) -> engine::render::CommandBuffer* {
                return id == secondary ? &secondary_commands : nullptr;
            },
    });

    engine::ui::UiCanvas canvas;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.rect = engine::render::Rect{8.0f, 16.0f, 32.0f, 48.0f};
    canvas.window = secondary;
    const engine::ecs::Entity hud = world.create();
    world.emplace<engine::ui::UiCanvas>(hud, canvas);

    world.run(engine::ecs::Schedule::Frame);

    EXPECT_TRUE(primary_commands.empty());
    ASSERT_EQ(secondary_commands.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawUI>(secondary_commands[0]));
    EXPECT_EQ(std::get<engine::render::CmdDrawUI>(secondary_commands[0]).rect, canvas.rect);
}

TEST(RenderSystem, UiPointerDoesNotLeakAcrossWindows) {
    const engine::WindowId window_b{3};
    engine::render::CommandBuffer commands_a;
    engine::render::CommandBuffer commands_b;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{
            .commands = &commands_a,
            .commands_for_window = [&](engine::WindowId id) -> engine::render::CommandBuffer* {
                return id == window_b ? &commands_b : nullptr;
            },
    });

    engine::ui::UiCanvas canvas_a;
    canvas_a.fit = engine::ui::UiFit::Fixed;
    canvas_a.rect = engine::render::Rect{0.0f, 0.0f, 100.0f, 100.0f};
    canvas_a.window = engine::kPrimaryWindow;
    world.emplace<engine::ui::UiCanvas>(world.create(), canvas_a);

    engine::ui::UiCanvas canvas_b = canvas_a;
    canvas_b.window = window_b;
    world.emplace<engine::ui::UiCanvas>(world.create(), canvas_b);

    // A down-click at the same local pixel offset, but only in window A.
    engine::ecs::EventWriter<engine::MouseEvent>{world}.send(engine::MouseEvent{
            .window = engine::kPrimaryWindow,
            .kind = engine::MouseEvent::Kind::Down,
            .position = {40.0f, 40.0f},
    });

    world.run(engine::ecs::Schedule::Frame);

    ASSERT_EQ(commands_a.size(), 1u);
    ASSERT_EQ(commands_b.size(), 1u);
    const auto& draw_a = std::get<engine::render::CmdDrawUI>(commands_a[0]);
    const auto& draw_b = std::get<engine::render::CmdDrawUI>(commands_b[0]);
    EXPECT_EQ(draw_a.pointer, (glm::vec2{40.0f, 40.0f}));
    EXPECT_TRUE(draw_a.pointer_down);
    // Window B never received a click â€” its pointer must stay at rest, not mirror window A's.
    EXPECT_EQ(draw_b.pointer, (glm::vec2{0.0f, 0.0f}));
    EXPECT_FALSE(draw_b.pointer_down);
}

TEST(RenderSystem, UiCanvasForSecondaryWindowSkippedWithoutCommandsForWindow) {
    engine::render::CommandBuffer primary_commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &primary_commands});

    engine::ui::UiCanvas canvas;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.rect = engine::render::Rect{8.0f, 16.0f, 32.0f, 48.0f};
    canvas.window = engine::WindowId{3};
    const engine::ecs::Entity hud = world.create();
    world.emplace<engine::ui::UiCanvas>(hud, canvas);

    EXPECT_NO_THROW(world.run(engine::ecs::Schedule::Frame));

    EXPECT_TRUE(primary_commands.empty());
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

    fakes::RecordingFatal fatal;
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

TEST(RenderSystem, BindPhaseInMemoryInstanceDoesNotCloneFromAssets) {
    constexpr std::string_view kHudGuid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1";

    TempDir tree;
    write_ui_asset(tree.path / "hud.xml", R"(<Canvas><Label text="FromAsset"/></Canvas>)", kHudGuid);

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kHudGuid}, "hud.xml", engine::ImporterKind::Ui});
    write_file(tree.path / "catalog.toml", catalog.serialize());

    fakes::RecordingFatal fatal;
    engine::AssetsDb db(fatal);
    const auto loaded = db.load_catalog(tree.path / "catalog.toml", tree.path);
    ASSERT_TRUE(loaded.has_value());

    auto vm = std::make_shared<TitleViewModel>();
    vm->title.set("Hello");
    const auto parsed =
            engine::ui::parse_xml(R"(<Canvas><Label class="runtime" text="{binding title}"/></Canvas>)", nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.fatal = &fatal, .assets = &db});

    engine::ui::UiCanvas canvas;
    canvas.data_context = vm;
    canvas.fit = engine::ui::UiFit::Fixed;
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed});

    world.run(engine::ecs::Schedule::Frame);
    const engine::ui::UiInstance* instance = world.try_get<engine::ui::UiInstance>(entity);
    ASSERT_NE(instance, nullptr);
    const engine::ui::Element* label =
            engine::ui::find_by_kind(instance->document.root, engine::ui::ElementKind::Label);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text, "Hello");
    ASSERT_EQ(label->classes.size(), 1u);
    EXPECT_EQ(label->classes[0], "runtime");
}

TEST(RenderSystem, BindPhaseSpawnCanvasDoesNotCloneFromAssets) {
    constexpr std::string_view kHudGuid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1";

    TempDir tree;
    write_ui_asset(tree.path / "hud.xml", R"(<Canvas><Label text="FromAsset"/></Canvas>)", kHudGuid);

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kHudGuid}, "hud.xml", engine::ImporterKind::Ui});
    write_file(tree.path / "catalog.toml", catalog.serialize());

    fakes::RecordingFatal fatal;
    engine::AssetsDb db(fatal);
    const auto loaded = db.load_catalog(tree.path / "catalog.toml", tree.path);
    ASSERT_TRUE(loaded.has_value());

    auto vm = std::make_shared<TitleViewModel>();
    vm->title.set("Hello");
    const auto parsed =
            engine::ui::parse_xml(R"(<Canvas><Label class="spawned" text="{binding title}"/></Canvas>)", nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.fatal = &fatal, .assets = &db});

    engine::ui::UiCanvas canvas;
    canvas.document = engine::AssetId{kHudGuid};
    canvas.data_context = vm;
    canvas.fit = engine::ui::UiFit::Fixed;
    const engine::ecs::Entity entity = engine::ui::spawn_canvas(world, canvas, *parsed);

    world.run(engine::ecs::Schedule::Frame);
    EXPECT_FALSE(world.get<engine::ui::UiCanvas>(entity).document.has_value());
    const engine::ui::UiInstance* instance = world.try_get<engine::ui::UiInstance>(entity);
    ASSERT_NE(instance, nullptr);
    const engine::ui::Element* label =
            engine::ui::find_by_kind(instance->document.root, engine::ui::ElementKind::Label);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text, "Hello");
    ASSERT_EQ(label->classes.size(), 1u);
    EXPECT_EQ(label->classes[0], "spawned");
}

TEST(RenderSystem, BindPhaseCatalogRebindsWithoutReplacingTreeWhenDataContextChanges) {
    constexpr std::string_view kHudGuid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1";

    TempDir tree;
    write_ui_asset(tree.path / "hud.xml", R"(<Canvas><Label class="keep" text="{binding title}"/></Canvas>)", kHudGuid);

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kHudGuid}, "hud.xml", engine::ImporterKind::Ui});
    write_file(tree.path / "catalog.toml", catalog.serialize());

    fakes::RecordingFatal fatal;
    engine::AssetsDb db(fatal);
    const auto loaded = db.load_catalog(tree.path / "catalog.toml", tree.path);
    ASSERT_TRUE(loaded.has_value());

    auto first = std::make_shared<TitleViewModel>();
    first->title.set("Hello");
    auto second = std::make_shared<TitleViewModel>();
    second->title.set("World");

    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.fatal = &fatal, .assets = &db});

    engine::ui::UiCanvas canvas;
    canvas.document = engine::AssetId{kHudGuid};
    canvas.data_context = first;
    canvas.fit = engine::ui::UiFit::Fixed;
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);

    world.run(engine::ecs::Schedule::Frame);
    engine::ui::Element* label = engine::ui::find_by_kind(
            world.get<engine::ui::UiInstance>(entity).document.root, engine::ui::ElementKind::Label);
    ASSERT_NE(label, nullptr);
    label->classes.push_back("stamped");

    world.get<engine::ui::UiCanvas>(entity).data_context = second;
    world.run(engine::ecs::Schedule::Frame);

    label = engine::ui::find_by_kind(
            world.get<engine::ui::UiInstance>(entity).document.root, engine::ui::ElementKind::Label);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text, "World");
    ASSERT_EQ(label->classes.size(), 2u);
    EXPECT_EQ(label->classes[1], "stamped");
}

TEST(RenderSystem, BindPhaseWithoutDocumentDoesNotLoadFromAssets) {
    constexpr std::string_view kHudGuid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1";

    TempDir tree;
    write_ui_asset(tree.path / "hud.xml", R"(<Canvas><Label text="FromAsset"/></Canvas>)", kHudGuid);

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kHudGuid}, "hud.xml", engine::ImporterKind::Ui});
    write_file(tree.path / "catalog.toml", catalog.serialize());

    fakes::RecordingFatal fatal;
    engine::AssetsDb db(fatal);
    const auto loaded = db.load_catalog(tree.path / "catalog.toml", tree.path);
    ASSERT_TRUE(loaded.has_value());

    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.fatal = &fatal, .assets = &db});

    engine::ui::UiCanvas canvas;
    canvas.fit = engine::ui::UiFit::Fixed;
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);

    world.run(engine::ecs::Schedule::Frame);
    EXPECT_EQ(world.try_get<engine::ui::UiInstance>(entity), nullptr);
}

TEST(RenderSystem, BindPhaseInMemoryKeepsAuthoredStylesheetWhenNoAssetSheets) {
    constexpr std::string_view kHudGuid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1";

    TempDir tree;
    write_ui_asset(tree.path / "hud.xml", R"(<Canvas><Label text="FromAsset"/></Canvas>)", kHudGuid);

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kHudGuid}, "hud.xml", engine::ImporterKind::Ui});
    write_file(tree.path / "catalog.toml", catalog.serialize());

    fakes::RecordingFatal fatal;
    engine::AssetsDb db(fatal);
    const auto loaded = db.load_catalog(tree.path / "catalog.toml", tree.path);
    ASSERT_TRUE(loaded.has_value());

    std::vector<std::string> warnings;
    auto parsed_sheet = engine::ui::parse_css("Label { color: #ff0000; }\n", warnings);
    ASSERT_TRUE(parsed_sheet.has_value());

    auto vm = std::make_shared<TitleViewModel>();
    const auto parsed = engine::ui::parse_xml(R"(<Canvas><Label text="Hi"/></Canvas>)", nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.fatal = &fatal, .assets = &db});

    engine::ui::UiCanvas canvas;
    canvas.data_context = vm;
    canvas.fit = engine::ui::UiFit::Fixed;
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed, *parsed_sheet});

    world.run(engine::ecs::Schedule::Frame);
    const engine::ui::UiInstance* instance = world.try_get<engine::ui::UiInstance>(entity);
    ASSERT_NE(instance, nullptr);
    ASSERT_TRUE(instance->stylesheet.has_value());
    ASSERT_EQ(instance->stylesheet->rules.size(), 1u);
    EXPECT_EQ(instance->stylesheet->rules[0].selector.element, "Label");
}

TEST(RenderSystem, BindXmlStylesheetWhenExtrasEmpty) {
    constexpr std::string_view kHudGuid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa3";
    constexpr std::string_view kCssGuid = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb01";

    TempDir tree;
    write_ui_asset(tree.path / "hud.xml",
            R"(<Canvas stylesheet="asset://bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb01"><Button content="Go"/></Canvas>)", kHudGuid);
    write_css_asset(tree.path / "red.css", "Button { width: 80; height: 40; background: #ff0000; }\n", kCssGuid);

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kHudGuid}, "hud.xml", engine::ImporterKind::Ui});
    catalog.add({engine::AssetId{kCssGuid}, "red.css", engine::ImporterKind::Css});
    write_file(tree.path / "catalog.toml", catalog.serialize());

    fakes::RecordingFatal fatal;
    engine::AssetsDb db(fatal);
    const auto loaded = db.load_catalog(tree.path / "catalog.toml", tree.path);
    ASSERT_TRUE(loaded.has_value());

    auto vm = std::make_shared<TitleViewModel>();
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.fatal = &fatal, .assets = &db});

    engine::ui::UiCanvas canvas;
    canvas.document = engine::AssetId{kHudGuid};
    canvas.data_context = vm;
    canvas.fit = engine::ui::UiFit::Fixed;
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);

    world.run(engine::ecs::Schedule::Frame);
    engine::ui::UiInstance* instance = world.try_get<engine::ui::UiInstance>(entity);
    ASSERT_NE(instance, nullptr);
    ASSERT_TRUE(instance->stylesheet);
    const glm::vec4 fill = paint_button_fill(*instance);
    EXPECT_NEAR(fill.r, 1.0f, 0.01f);
    EXPECT_NEAR(fill.g, 0.0f, 0.01f);
    EXPECT_NEAR(fill.b, 0.0f, 0.01f);
}

TEST(RenderSystem, BindMergesExtraStylesheets) {
    constexpr std::string_view kHudGuid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa4";
    constexpr std::string_view kCssRed = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb03";
    constexpr std::string_view kCssGreen = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb04";

    TempDir tree;
    write_ui_asset(tree.path / "hud.xml", R"(<Canvas><Button content="Go"/></Canvas>)", kHudGuid);
    write_css_asset(tree.path / "red.css", "Button { width: 80; height: 40; background: #ff0000; }\n", kCssRed);
    write_css_asset(tree.path / "green.css", "Button { background: #00ff00; }\n", kCssGreen);

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kHudGuid}, "hud.xml", engine::ImporterKind::Ui});
    catalog.add({engine::AssetId{kCssRed}, "red.css", engine::ImporterKind::Css});
    catalog.add({engine::AssetId{kCssGreen}, "green.css", engine::ImporterKind::Css});
    write_file(tree.path / "catalog.toml", catalog.serialize());

    fakes::RecordingFatal fatal;
    engine::AssetsDb db(fatal);
    const auto loaded = db.load_catalog(tree.path / "catalog.toml", tree.path);
    ASSERT_TRUE(loaded.has_value());

    auto vm = std::make_shared<TitleViewModel>();
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.fatal = &fatal, .assets = &db});

    engine::ui::UiCanvas canvas;
    canvas.document = engine::AssetId{kHudGuid};
    canvas.stylesheet = engine::AssetId{kCssRed};
    canvas.extra_stylesheets = {engine::AssetId{kCssGreen}};
    canvas.data_context = vm;
    canvas.fit = engine::ui::UiFit::Fixed;
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);

    world.run(engine::ecs::Schedule::Frame);
    engine::ui::UiInstance* instance = world.try_get<engine::ui::UiInstance>(entity);
    ASSERT_NE(instance, nullptr);
    ASSERT_TRUE(instance->stylesheet);
    const glm::vec4 fill = paint_button_fill(*instance);
    EXPECT_NEAR(fill.r, 0.0f, 0.01f);
    EXPECT_NEAR(fill.g, 1.0f, 0.01f);
    EXPECT_NEAR(fill.b, 0.0f, 0.01f);
}

TEST(RenderSystem, BindMergesKeyframesOfEveryStylesheet) {
    constexpr std::string_view kHudGuid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa41";
    constexpr std::string_view kCssBase = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb41";
    constexpr std::string_view kCssExtra = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb42";

    TempDir tree;
    write_ui_asset(tree.path / "hud.xml", R"(<Canvas><Button content="Go"/></Canvas>)", kHudGuid);
    write_css_asset(tree.path / "base.css",
            "Button { width: 80; height: 40; animation-name: pulse; animation-duration: 1s; }\n"
            "@keyframes pulse { from { opacity: 1; } to { opacity: 0; } }\n"
            "@keyframes spin { from { opacity: 1; } to { opacity: 0.5; } }\n",
            kCssBase);
    write_css_asset(tree.path / "extra.css", "@keyframes spin { from { opacity: 0; } to { opacity: 1; } }\n", kCssExtra);

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kHudGuid}, "hud.xml", engine::ImporterKind::Ui});
    catalog.add({engine::AssetId{kCssBase}, "base.css", engine::ImporterKind::Css});
    catalog.add({engine::AssetId{kCssExtra}, "extra.css", engine::ImporterKind::Css});
    write_file(tree.path / "catalog.toml", catalog.serialize());

    fakes::RecordingFatal fatal;
    engine::AssetsDb db(fatal);
    const auto loaded = db.load_catalog(tree.path / "catalog.toml", tree.path);
    ASSERT_TRUE(loaded.has_value());

    auto vm = std::make_shared<TitleViewModel>();
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.fatal = &fatal, .assets = &db});

    engine::ui::UiCanvas canvas;
    canvas.document = engine::AssetId{kHudGuid};
    canvas.stylesheet = engine::AssetId{kCssBase};
    canvas.extra_stylesheets = {engine::AssetId{kCssExtra}};
    canvas.data_context = vm;
    canvas.fit = engine::ui::UiFit::Fixed;
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);

    world.run(engine::ecs::Schedule::Frame);
    engine::ui::UiInstance* instance = world.try_get<engine::ui::UiInstance>(entity);
    ASSERT_NE(instance, nullptr);
    ASSERT_TRUE(instance->stylesheet);
    const std::vector<engine::ui::Keyframes>& keyframes = instance->stylesheet->keyframes;
    ASSERT_EQ(keyframes.size(), 2u) << "an asset canvas keeps @keyframes, and one name stays one block";
    EXPECT_EQ(keyframes[0].name, "pulse");
    EXPECT_EQ(keyframes[1].name, "spin");
    ASSERT_FALSE(keyframes[1].stops.empty());
    ASSERT_FALSE(keyframes[1].stops[0].declarations.empty());
    EXPECT_EQ(keyframes[1].stops[0].declarations[0].value, "0") << "the later sheet's block wins";
}

TEST(RenderSystem, BindReloadsWhenExtraStylesheetsChange) {
    constexpr std::string_view kHudGuid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa5";
    constexpr std::string_view kCssRed = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb05";
    constexpr std::string_view kCssGreen = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb06";

    TempDir tree;
    write_ui_asset(tree.path / "hud.xml", R"(<Canvas><Button content="Go"/></Canvas>)", kHudGuid);
    write_css_asset(tree.path / "red.css", "Button { width: 80; height: 40; background: #ff0000; }\n", kCssRed);
    write_css_asset(tree.path / "green.css", "Button { background: #00ff00; }\n", kCssGreen);

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kHudGuid}, "hud.xml", engine::ImporterKind::Ui});
    catalog.add({engine::AssetId{kCssRed}, "red.css", engine::ImporterKind::Css});
    catalog.add({engine::AssetId{kCssGreen}, "green.css", engine::ImporterKind::Css});
    write_file(tree.path / "catalog.toml", catalog.serialize());

    fakes::RecordingFatal fatal;
    engine::AssetsDb db(fatal);
    const auto loaded = db.load_catalog(tree.path / "catalog.toml", tree.path);
    ASSERT_TRUE(loaded.has_value());

    auto vm = std::make_shared<TitleViewModel>();
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.fatal = &fatal, .assets = &db});

    engine::ui::UiCanvas canvas;
    canvas.document = engine::AssetId{kHudGuid};
    canvas.extra_stylesheets = {engine::AssetId{kCssRed}};
    canvas.data_context = vm;
    canvas.fit = engine::ui::UiFit::Fixed;
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);

    world.run(engine::ecs::Schedule::Frame);
    engine::ui::UiInstance* instance = world.try_get<engine::ui::UiInstance>(entity);
    ASSERT_NE(instance, nullptr);
    glm::vec4 fill = paint_button_fill(*instance);
    EXPECT_NEAR(fill.r, 1.0f, 0.01f);
    EXPECT_NEAR(fill.g, 0.0f, 0.01f);

    world.get<engine::ui::UiCanvas>(entity).extra_stylesheets.push_back(engine::AssetId{kCssGreen});
    world.run(engine::ecs::Schedule::Frame);
    instance = world.try_get<engine::ui::UiInstance>(entity);
    ASSERT_NE(instance, nullptr);
    fill = paint_button_fill(*instance);
    EXPECT_NEAR(fill.r, 0.0f, 0.01f);
    EXPECT_NEAR(fill.g, 1.0f, 0.01f);
}

TEST(RenderSystem, BindSkipsMissingExtraStylesheet) {
    constexpr std::string_view kHudGuid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa6";
    constexpr std::string_view kCssRed = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb07";
    constexpr std::string_view kMissing = "cccccccccccccccccccccccccccccc01";

    TempDir tree;
    write_ui_asset(tree.path / "hud.xml", R"(<Canvas><Button content="Go"/></Canvas>)", kHudGuid);
    write_css_asset(tree.path / "red.css", "Button { width: 80; height: 40; background: #ff0000; }\n", kCssRed);

    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kHudGuid}, "hud.xml", engine::ImporterKind::Ui});
    catalog.add({engine::AssetId{kCssRed}, "red.css", engine::ImporterKind::Css});
    write_file(tree.path / "catalog.toml", catalog.serialize());

    fakes::RecordingFatal fatal;
    engine::AssetsDb db(fatal);
    const auto loaded = db.load_catalog(tree.path / "catalog.toml", tree.path);
    ASSERT_TRUE(loaded.has_value());

    auto vm = std::make_shared<TitleViewModel>();
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.fatal = &fatal, .assets = &db});

    engine::ui::UiCanvas canvas;
    canvas.document = engine::AssetId{kHudGuid};
    canvas.stylesheet = engine::AssetId{kCssRed};
    canvas.extra_stylesheets = {engine::AssetId{kMissing}};
    canvas.data_context = vm;
    canvas.fit = engine::ui::UiFit::Fixed;
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);

    world.run(engine::ecs::Schedule::Frame);
    EXPECT_EQ(fatal.call_count, 0);
    engine::ui::UiInstance* instance = world.try_get<engine::ui::UiInstance>(entity);
    ASSERT_NE(instance, nullptr);
    ASSERT_TRUE(instance->stylesheet);
    const glm::vec4 fill = paint_button_fill(*instance);
    EXPECT_NEAR(fill.r, 1.0f, 0.01f);
    EXPECT_NEAR(fill.g, 0.0f, 0.01f);
}

TEST(RenderSystem, ModelMatrixIsTranslateRotateScale) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_camera(world);
    // The entity sits at z = 3, past the default -1..1 clip range.
    engine::Camera& lens = world.get<engine::Camera>(world.ctx<engine::ActiveCamera>().entity);
    lens.near_clip = -10.0f;
    lens.far_clip = 10.0f;

    const auto mesh = std::make_shared<FakeMesh>();
    const auto material = std::make_shared<FakeMaterial>();
    engine::Transform transform;
    transform.position = {1.0f, 2.0f, 3.0f};
    transform.rotation = engine::euler_xyz({0.1f, 0.2f, 0.3f});
    transform.scale = {2.0f, 3.0f, 4.0f};

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, transform);
    world.emplace<engine::render::Renderable>(entity, make_renderable(mesh, material, 0));

    world.run(engine::ecs::Schedule::Frame);

    ASSERT_EQ(commands.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(commands[1]));
    const glm::mat4 model = std::get<engine::render::CmdDrawMesh>(commands[1]).model;
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

    fakes::RecordingFatal fatal;
    engine::AssetsDb db(fatal);
    db.set_root(dir.path);
    const engine::AssetId id{"b1c2d3e4f567890123456789012345ab"};
    engine::CookedCatalog catalog;
    catalog.add({id, "sfx/step.wav", engine::ImporterKind::Audio});
    db.set_catalog(std::move(catalog));

    engine::AudioSystem audio;

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
    fakes::RecordingFatal fatal;
    engine::AssetsDb db(fatal);
    engine::AudioSystem audio;

    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.assets = &db, .audio = &audio});
    engine::ecs::EventWriter<engine::PlaySfxEvent>{world}.send(engine::PlaySfxEvent{
            .id = engine::AssetId{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
            .volume_scale = 0.5f,
    });
    EXPECT_THROW(world.run(engine::ecs::Schedule::Frame), std::runtime_error);
    EXPECT_EQ(fatal.call_count, 1);
}

TEST(Audio, HeadlessWorldIgnoresSoundEvents) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    const engine::AssetId id{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
    engine::ecs::EventWriter<engine::PlaySfxEvent>{world}.send(engine::PlaySfxEvent{.id = id, .volume_scale = 1.0f});
    engine::ecs::EventWriter<engine::PlayMusicEvent>{world}.send(
            engine::PlayMusicEvent{.id = id, .loop = true, .fade_seconds = 0.0f});

    EXPECT_NO_THROW(world.run(engine::ecs::Schedule::Frame));
}

TEST(Audio, AssetsAloneRegisterNoAudioSystem) {
    fakes::RecordingFatal fatal;
    engine::AssetsDb db(fatal);
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.fatal = &fatal, .assets = &db});
    engine::ecs::EventWriter<engine::PlaySfxEvent>{world}.send(engine::PlaySfxEvent{
            .id = engine::AssetId{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
            .volume_scale = 0.5f,
    });

    // The cue is not in the catalog, but nothing reads the event without an audio device.
    EXPECT_NO_THROW(world.run(engine::ecs::Schedule::Frame));
    EXPECT_EQ(fatal.call_count, 0);
}

TEST(RenderSystem, MissingStylesheetWarnsOncePerWorldAcrossFrames) {
    constexpr std::string_view kHudGuid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa7";
    constexpr std::string_view kMissing = "cccccccccccccccccccccccccccccc02";

    TempDir tree;
    write_ui_asset(tree.path / "hud.xml", R"(<Canvas><Button content="Go"/></Canvas>)", kHudGuid);
    engine::CookedCatalog catalog;
    catalog.add({engine::AssetId{kHudGuid}, "hud.xml", engine::ImporterKind::Ui});
    write_file(tree.path / "catalog.toml", catalog.serialize());

    fakes::RecordingFatal fatal;
    engine::AssetsDb db(fatal);
    ASSERT_TRUE(db.load_catalog(tree.path / "catalog.toml", tree.path).has_value());

    const std::filesystem::path log_dir = tree.path / "log";
    std::filesystem::create_directories(log_dir);
    engine::log::init(log_dir);

    auto vm = std::make_shared<TitleViewModel>();
    for (int round = 0; round < 2; ++round) {
        engine::ecs::World world;
        engine::register_engine_systems(world, engine::EngineSystemDeps{.fatal = &fatal, .assets = &db});
        engine::ui::UiCanvas canvas;
        canvas.document = engine::AssetId{kHudGuid};
        canvas.extra_stylesheets = {engine::AssetId{kMissing}};
        canvas.data_context = vm;
        canvas.fit = engine::ui::UiFit::Fixed;
        world.emplace<engine::ui::UiCanvas>(world.create(), canvas);
        for (int frame = 0; frame < 4; ++frame) {
            world.run(engine::ecs::Schedule::Frame);
        }
    }
    engine::log::init();

    std::ifstream in(log_dir / "game.log");
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::size_t warnings = 0;
    for (std::size_t at = text.find(kMissing); at != std::string::npos; at = text.find(kMissing, at + 1)) {
        ++warnings;
    }
    // Once for each of the two worlds, not once per frame.
    EXPECT_EQ(warnings, 2u);
}

namespace {

class StateMaterial final : public engine::render::IMaterial {
public:
    explicit StateMaterial(engine::render::RenderState state)
        : state_(state) {}

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
        return state_;
    }

private:
    engine::render::RenderState state_;
};

engine::ecs::Entity spawn_sprite_at(engine::ecs::World& world, std::shared_ptr<engine::render::IMesh> mesh,
        std::shared_ptr<engine::render::IMaterial> material, float z) {
    const engine::ecs::Entity entity = world.create();
    engine::Transform transform{};
    transform.position = {0.0f, 0.0f, z};
    world.emplace<engine::Transform>(entity, transform);
    engine::render::Sprite sprite{};
    sprite.mesh = std::move(mesh);
    sprite.material = std::move(material);
    world.emplace<engine::render::Sprite>(entity, std::move(sprite));
    return entity;
}

engine::ecs::Entity spawn_mesh_at(engine::ecs::World& world, std::shared_ptr<engine::render::IMesh> mesh,
        std::shared_ptr<engine::render::IMaterial> material, float z, int layer = 0) {
    const engine::ecs::Entity entity = world.create();
    engine::Transform transform{};
    transform.position = {0.0f, 0.0f, z};
    world.emplace<engine::Transform>(entity, transform);
    world.emplace<engine::render::Renderable>(entity, make_renderable(std::move(mesh), std::move(material), layer));
    return entity;
}

// A perspective camera at z = 10 looking down -Z.
void spawn_perspective_camera(engine::ecs::World& world) {
    spawn_camera(world);
    const engine::ecs::Entity camera = world.ctx<engine::ActiveCamera>().entity;
    world.get<engine::Transform>(camera).position = {0.0f, 0.0f, 10.0f};
    engine::Camera& lens = world.get<engine::Camera>(camera);
    lens.projection = engine::Projection::Perspective;
    lens.near_clip = 0.1f;
    lens.far_clip = 100.0f;
}

std::vector<std::shared_ptr<engine::render::IMesh>> drawn_meshes(const engine::render::CommandBuffer& commands) {
    std::vector<std::shared_ptr<engine::render::IMesh>> meshes;
    for (std::size_t i = 0; i < commands.size(); ++i) {
        if (const auto* mesh = std::get_if<engine::render::CmdDrawMesh>(&commands[i])) {
            meshes.push_back(mesh->mesh);
        }
    }
    return meshes;
}

struct SceneDraw {
    std::shared_ptr<engine::render::IMesh> mesh;
    const engine::render::CmdDrawParticles* particles = nullptr;
};

std::vector<SceneDraw> scene_draws(const engine::render::CommandBuffer& commands) {
    std::vector<SceneDraw> draws;
    for (std::size_t i = 0; i < commands.size(); ++i) {
        if (const auto* mesh = std::get_if<engine::render::CmdDrawMesh>(&commands[i])) {
            draws.push_back(SceneDraw{.mesh = mesh->mesh});
        } else if (const auto* particles = std::get_if<engine::render::CmdDrawParticles>(&commands[i])) {
            draws.push_back(SceneDraw{.particles = particles});
        }
    }
    return draws;
}

engine::ecs::Entity spawn_emitter(engine::ecs::World& world, std::shared_ptr<engine::render::IMaterial> material,
        engine::render::ParticleDraw draw) {
    const engine::ecs::Entity entity = world.create();
    engine::render::ParticleEmitter& emitter = world.emplace<engine::render::ParticleEmitter>(entity);
    emitter.mesh = std::make_shared<FakeMesh>();
    emitter.material = std::move(material);
    emitter.draw = draw;
    emitter.emission_rate = 0.0f;
    return entity;
}

void push_particle(engine::render::ParticleEmitter& emitter, glm::vec3 position) {
    engine::render::Particle particle;
    particle.position = position;
    emitter.particles.push_back(particle);
}

class DeclaresShader final : public engine::render::IShader {
public:
    explicit DeclaresShader(bool declares)
        : declares_(declares) {}

    bool declares_particle_instance_inputs() const override {
        return declares_;
    }

private:
    bool declares_;
};

class ShaderStateMaterial final : public engine::render::IMaterial {
public:
    ShaderStateMaterial(std::shared_ptr<engine::render::IShader> shader, engine::render::RenderState state)
        : shader_(std::move(shader))
        , state_(state) {}

    std::shared_ptr<engine::render::IShader> shader() const override {
        return shader_;
    }

    std::shared_ptr<engine::render::ITexture> texture(int) const override {
        return {};
    }

    glm::vec4 color() const override {
        return {1.0f, 1.0f, 1.0f, 1.0f};
    }

    engine::render::RenderState render_state() const override {
        return state_;
    }

private:
    std::shared_ptr<engine::render::IShader> shader_;
    engine::render::RenderState state_;
};

}

// 2D regression: sprites with the same layer and order but different z keep material, then entity order, under an
// orthographic and under a perspective camera alike.
TEST(RenderSystem, TwoDimensionalSpritesAtDifferentZKeepMaterialThenEntityOrder) {
    for (const bool perspective : {false, true}) {
        engine::render::CommandBuffer commands;
        engine::ecs::World world;
        engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
        if (perspective) {
            spawn_perspective_camera(world);
        } else {
            spawn_camera(world);
        }

        auto first = std::make_shared<StateMaterial>(engine::render::RenderState{});
        auto second = std::make_shared<StateMaterial>(
                engine::render::RenderState{.blend = engine::render::BlendMode::Opaque});
        if (std::less<>{}(second.get(), first.get())) {
            std::swap(first, second);
        }
        const auto mesh_a = std::make_shared<FakeMesh>();
        const auto mesh_b = std::make_shared<FakeMesh>();
        const auto mesh_c = std::make_shared<FakeMesh>();
        const auto mesh_d = std::make_shared<FakeMesh>();
        spawn_sprite_at(world, mesh_a, second, 0.9f);
        spawn_sprite_at(world, mesh_b, first, -0.9f);
        spawn_sprite_at(world, mesh_c, second, -0.5f);
        spawn_sprite_at(world, mesh_d, first, 0.5f);

        world.run(engine::ecs::Schedule::Frame);

        const auto meshes = drawn_meshes(commands);
        ASSERT_EQ(meshes.size(), 4u) << "perspective " << perspective;
        EXPECT_EQ(meshes[0], mesh_b) << "perspective " << perspective;
        EXPECT_EQ(meshes[1], mesh_d) << "perspective " << perspective;
        EXPECT_EQ(meshes[2], mesh_a) << "perspective " << perspective;
        EXPECT_EQ(meshes[3], mesh_c) << "perspective " << perspective;
    }
}

TEST(RenderSystem, OpaqueQueueFrontToBackBeforeTransparentBackToFront) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_perspective_camera(world);

    const auto opaque = std::make_shared<StateMaterial>(engine::render::RenderState{
            .blend = engine::render::BlendMode::Opaque,
            .depth_test = true,
            .depth_write = true,
            .cull = engine::render::CullMode::Back,
    });
    const auto glass = std::make_shared<StateMaterial>(engine::render::RenderState{
            .blend = engine::render::BlendMode::Alpha,
            .depth_test = true,
    });
    const auto glass_near = std::make_shared<FakeMesh>();
    const auto glass_far = std::make_shared<FakeMesh>();
    const auto cube_far = std::make_shared<FakeMesh>();
    const auto cube_near = std::make_shared<FakeMesh>();
    spawn_mesh_at(world, glass_near, glass, 5.0f, -1);
    spawn_mesh_at(world, glass_far, glass, -5.0f, -1);
    spawn_mesh_at(world, cube_far, opaque, -3.0f, 4);
    spawn_mesh_at(world, cube_near, opaque, 3.0f, 4);

    world.run(engine::ecs::Schedule::Frame);

    const auto meshes = drawn_meshes(commands);
    ASSERT_EQ(meshes.size(), 4u);
    EXPECT_EQ(meshes[0], cube_near);
    EXPECT_EQ(meshes[1], cube_far);
    EXPECT_EQ(meshes[2], glass_far);
    EXPECT_EQ(meshes[3], glass_near);
}

// Camera at z = 10, identity rotation: view depth is 10 - z. Larger is farther, and back to front draws that first.
TEST(RenderSystem, PlaneEmitterStaysUnorderedDespiteDepthMaterial) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_perspective_camera(world);
    world.ctx<engine::Time>().delta_time = 0.0f;

    const auto card = std::make_shared<StateMaterial>(engine::render::RenderState{
            .blend = engine::render::BlendMode::Alpha,
            .depth_test = true,
            .depth_write = true,
            .cull = engine::render::CullMode::Back,
    });
    const auto glass = std::make_shared<StateMaterial>(engine::render::RenderState{
            .blend = engine::render::BlendMode::Alpha,
            .depth_test = true,
    });
    const auto glass_far = std::make_shared<FakeMesh>();
    const auto glass_near = std::make_shared<FakeMesh>();
    spawn_mesh_at(world, glass_far, glass, -1.0f);
    spawn_mesh_at(world, glass_near, glass, 6.0f);
    engine::render::ParticleEmitter& emitter =
            world.get<engine::render::ParticleEmitter>(spawn_emitter(world, card, engine::render::ParticleDraw::Plane));
    push_particle(emitter, {0.0f, 0.0f, 4.0f});
    push_particle(emitter, {0.0f, 0.0f, -4.0f});
    push_particle(emitter, {0.0f, 0.0f, 0.0f});

    world.run(engine::ecs::Schedule::Frame);

    const auto draws = scene_draws(commands);
    ASSERT_EQ(draws.size(), 3u);
    EXPECT_EQ(draws[0].mesh, glass_far);
    EXPECT_EQ(draws[1].mesh, glass_near);
    ASSERT_NE(draws[2].particles, nullptr);
    EXPECT_EQ(draws[2].particles->draw, engine::render::ParticleDraw::Plane);
    EXPECT_TRUE(draws[2].particles->mesh_instances.empty());
    ASSERT_EQ(draws[2].particles->instances.size(), 3u);
    EXPECT_EQ(draws[2].particles->instances[0].position, (glm::vec3{0.0f, 0.0f, 4.0f}));
    EXPECT_EQ(draws[2].particles->instances[1].position, (glm::vec3{0.0f, 0.0f, -4.0f}));
    EXPECT_EQ(draws[2].particles->instances[2].position, (glm::vec3{0.0f, 0.0f, 0.0f}));
}

TEST(RenderSystem, BillboardAndStretchedDepthTestSortsInstancesAndKey) {
    const engine::render::RenderState state{
            .blend = engine::render::BlendMode::Alpha,
            .depth_test = true,
            .cull = engine::render::CullMode::Back,
    };
    for (const engine::render::ParticleDraw draw :
            {engine::render::ParticleDraw::Billboard, engine::render::ParticleDraw::Stretched}) {
        engine::render::CommandBuffer commands;
        engine::ecs::World world;
        engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
        spawn_perspective_camera(world);
        world.ctx<engine::Time>().delta_time = 0.0f;

        const auto card = std::make_shared<StateMaterial>(state);
        const auto glass = std::make_shared<StateMaterial>(engine::render::RenderState{
                .blend = engine::render::BlendMode::Alpha,
                .depth_test = true,
        });
        const auto glass_far = std::make_shared<FakeMesh>();
        const auto glass_near = std::make_shared<FakeMesh>();
        spawn_mesh_at(world, glass_far, glass, -1.0f);
        spawn_mesh_at(world, glass_near, glass, 6.0f);
        engine::render::ParticleEmitter& emitter =
                world.get<engine::render::ParticleEmitter>(spawn_emitter(world, card, draw));
        push_particle(emitter, {1.0f, 0.0f, -4.0f});
        push_particle(emitter, {0.0f, 0.0f, 4.0f});
        push_particle(emitter, {2.0f, 0.0f, -4.0f});

        world.run(engine::ecs::Schedule::Frame);

        const auto draws = scene_draws(commands);
        ASSERT_EQ(draws.size(), 3u) << static_cast<int>(draw);
        ASSERT_NE(draws[0].particles, nullptr) << static_cast<int>(draw);
        EXPECT_EQ(draws[1].mesh, glass_far) << static_cast<int>(draw);
        EXPECT_EQ(draws[2].mesh, glass_near) << static_cast<int>(draw);
        const engine::render::CmdDrawParticles& cmd = *draws[0].particles;
        EXPECT_EQ(cmd.draw, draw);
        EXPECT_EQ(cmd.blend, engine::render::BlendMode::Alpha);
        EXPECT_EQ(cmd.material->render_state(), state);
        ASSERT_EQ(cmd.instances.size(), 3u);
        EXPECT_EQ(cmd.instances[0].position, (glm::vec3{1.0f, 0.0f, -4.0f}));
        EXPECT_EQ(cmd.instances[1].position, (glm::vec3{2.0f, 0.0f, -4.0f}));
        EXPECT_EQ(cmd.instances[2].position, (glm::vec3{0.0f, 0.0f, 4.0f}));
    }
}

// RenderState{} is the depth-off state run_render stores for a texture (blend only, depth and cull left off).
TEST(RenderSystem, BillboardWithDepthOffMaterialStaysUnordered) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_perspective_camera(world);
    world.ctx<engine::Time>().delta_time = 0.0f;

    const auto card = std::make_shared<StateMaterial>(engine::render::RenderState{});
    const auto glass = std::make_shared<StateMaterial>(engine::render::RenderState{
            .blend = engine::render::BlendMode::Alpha,
            .depth_test = true,
    });
    const auto glass_far = std::make_shared<FakeMesh>();
    const auto glass_near = std::make_shared<FakeMesh>();
    spawn_mesh_at(world, glass_far, glass, -1.0f);
    spawn_mesh_at(world, glass_near, glass, 6.0f);
    engine::render::ParticleEmitter& emitter = world.get<engine::render::ParticleEmitter>(
            spawn_emitter(world, card, engine::render::ParticleDraw::Billboard));
    push_particle(emitter, {0.0f, 0.0f, 4.0f});
    push_particle(emitter, {0.0f, 0.0f, -4.0f});
    push_particle(emitter, {0.0f, 0.0f, 0.0f});

    world.run(engine::ecs::Schedule::Frame);

    const auto draws = scene_draws(commands);
    ASSERT_EQ(draws.size(), 3u);
    EXPECT_EQ(draws[0].mesh, glass_far);
    EXPECT_EQ(draws[1].mesh, glass_near);
    ASSERT_NE(draws[2].particles, nullptr);
    ASSERT_EQ(draws[2].particles->instances.size(), 3u);
    EXPECT_EQ(draws[2].particles->instances[0].position, (glm::vec3{0.0f, 0.0f, 4.0f}));
    EXPECT_EQ(draws[2].particles->instances[1].position, (glm::vec3{0.0f, 0.0f, -4.0f}));
    EXPECT_EQ(draws[2].particles->instances[2].position, (glm::vec3{0.0f, 0.0f, 0.0f}));
}

TEST(RenderSystem, OpaqueBillboardKeepsSpawnOrderAndKeysFromFarthest) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_perspective_camera(world);
    world.ctx<engine::Time>().delta_time = 0.0f;

    const auto opaque = std::make_shared<StateMaterial>(engine::render::RenderState{
            .blend = engine::render::BlendMode::Opaque,
            .depth_test = true,
            .depth_write = true,
            .cull = engine::render::CullMode::Back,
    });
    const auto glass = std::make_shared<StateMaterial>(engine::render::RenderState{
            .blend = engine::render::BlendMode::Alpha,
            .depth_test = true,
    });
    const auto between = std::make_shared<FakeMesh>();
    const auto glass_mesh = std::make_shared<FakeMesh>();
    engine::render::ParticleEmitter& far_cloud = world.get<engine::render::ParticleEmitter>(
            spawn_emitter(world, opaque, engine::render::ParticleDraw::Billboard));
    push_particle(far_cloud, {0.0f, 0.0f, 2.0f});
    push_particle(far_cloud, {0.0f, 0.0f, -6.0f});
    spawn_mesh_at(world, between, opaque, -2.0f);
    engine::render::ParticleEmitter& near_cloud = world.get<engine::render::ParticleEmitter>(
            spawn_emitter(world, opaque, engine::render::ParticleDraw::Billboard));
    push_particle(near_cloud, {0.0f, 0.0f, 8.0f});
    spawn_mesh_at(world, glass_mesh, glass, 0.0f);

    world.run(engine::ecs::Schedule::Frame);

    const auto draws = scene_draws(commands);
    ASSERT_EQ(draws.size(), 4u);
    ASSERT_NE(draws[0].particles, nullptr);
    EXPECT_EQ(draws[0].particles->instances[0].position, (glm::vec3{0.0f, 0.0f, 8.0f}));
    EXPECT_EQ(draws[1].mesh, between);
    ASSERT_NE(draws[2].particles, nullptr);
    ASSERT_EQ(draws[2].particles->instances.size(), 2u);
    EXPECT_EQ(draws[2].particles->instances[0].position, (glm::vec3{0.0f, 0.0f, 2.0f}));
    EXPECT_EQ(draws[2].particles->instances[1].position, (glm::vec3{0.0f, 0.0f, -6.0f}));
    EXPECT_EQ(draws[3].mesh, glass_mesh);
}

TEST(RenderSystem, PlaneWithOpaqueMaterialStaysTransparentAndUnordered) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_perspective_camera(world);
    world.ctx<engine::Time>().delta_time = 0.0f;

    const auto opaque = std::make_shared<StateMaterial>(engine::render::RenderState{
            .blend = engine::render::BlendMode::Opaque,
            .depth_test = true,
            .depth_write = true,
            .cull = engine::render::CullMode::Back,
    });
    const auto cube = std::make_shared<FakeMesh>();
    spawn_mesh_at(world, cube, opaque, 0.0f);
    engine::render::ParticleEmitter& emitter = world.get<engine::render::ParticleEmitter>(
            spawn_emitter(world, opaque, engine::render::ParticleDraw::Plane));
    push_particle(emitter, {0.0f, 0.0f, 8.0f});
    push_particle(emitter, {0.0f, 0.0f, 6.0f});

    world.run(engine::ecs::Schedule::Frame);

    const auto draws = scene_draws(commands);
    ASSERT_EQ(draws.size(), 2u);
    EXPECT_EQ(draws[0].mesh, cube);
    ASSERT_NE(draws[1].particles, nullptr);
    ASSERT_EQ(draws[1].particles->instances.size(), 2u);
    EXPECT_EQ(draws[1].particles->instances[0].position, (glm::vec3{0.0f, 0.0f, 8.0f}));
    EXPECT_EQ(draws[1].particles->instances[1].position, (glm::vec3{0.0f, 0.0f, 6.0f}));
}

TEST(RenderSystem, TransparentMeshSortsInstancesAndScalesFromMeshScale) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_perspective_camera(world);
    world.ctx<engine::Time>().delta_time = 0.0f;

    const auto cube = std::make_shared<StateMaterial>(engine::render::RenderState{
            .blend = engine::render::BlendMode::Alpha,
            .depth_test = true,
            .cull = engine::render::CullMode::Back,
    });
    const auto glass = std::make_shared<StateMaterial>(engine::render::RenderState{
            .blend = engine::render::BlendMode::Alpha,
            .depth_test = true,
    });
    const auto glass_far = std::make_shared<FakeMesh>();
    const auto glass_near = std::make_shared<FakeMesh>();
    spawn_mesh_at(world, glass_far, glass, -1.0f);
    spawn_mesh_at(world, glass_near, glass, 6.0f);
    const engine::ecs::Entity entity = spawn_emitter(world, cube, engine::render::ParticleDraw::Mesh);
    engine::render::ParticleEmitter& emitter = world.get<engine::render::ParticleEmitter>(entity);
    const std::shared_ptr<engine::render::IMesh> mesh = emitter.mesh;
    emitter.mesh_scale = {2.0f, 3.0f, 4.0f};
    push_particle(emitter, {1.0f, 0.0f, -4.0f});
    push_particle(emitter, {0.0f, 0.0f, 4.0f});
    push_particle(emitter, {2.0f, 0.0f, -4.0f});
    for (engine::render::Particle& particle : emitter.particles) {
        particle.size = {0.0f, 0.0f};
        particle.texture_layer = 2.0f;
        particle.color = {1.0f, 0.0f, 0.0f, 0.5f};
    }

    world.run(engine::ecs::Schedule::Frame);

    const auto draws = scene_draws(commands);
    ASSERT_EQ(draws.size(), 3u);
    ASSERT_NE(draws[0].particles, nullptr);
    EXPECT_EQ(draws[1].mesh, glass_far);
    EXPECT_EQ(draws[2].mesh, glass_near);
    const engine::render::CmdDrawParticles& cmd = *draws[0].particles;
    EXPECT_EQ(cmd.draw, engine::render::ParticleDraw::Mesh);
    EXPECT_TRUE(cmd.instances.empty());
    EXPECT_EQ(cmd.mesh, mesh);
    ASSERT_EQ(cmd.mesh_instances.size(), 3u);
    EXPECT_EQ(cmd.mesh_instances[0].position, (glm::vec3{1.0f, 0.0f, -4.0f}));
    EXPECT_EQ(cmd.mesh_instances[1].position, (glm::vec3{2.0f, 0.0f, -4.0f}));
    EXPECT_EQ(cmd.mesh_instances[2].position, (glm::vec3{0.0f, 0.0f, 4.0f}));
    for (const engine::render::MeshParticleInstance& instance : cmd.mesh_instances) {
        EXPECT_EQ(instance.scale, (glm::vec3{2.0f, 3.0f, 4.0f}));
        EXPECT_FLOAT_EQ(instance.layer, 2.0f);
        EXPECT_EQ(instance.color, (glm::vec4{1.0f, 0.0f, 0.0f, 0.5f}));
    }
}

TEST(RenderSystem, MeshSizeCurveScalesTheInstanceAndIgnoresCardSize) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_perspective_camera(world);
    world.ctx<engine::Time>().delta_time = 0.0f;

    const auto cube = std::make_shared<StateMaterial>(engine::render::RenderState{
            .blend = engine::render::BlendMode::Opaque,
            .depth_test = true,
            .depth_write = true,
    });
    engine::render::ParticleEmitter& emitter = world.get<engine::render::ParticleEmitter>(
            spawn_emitter(world, cube, engine::render::ParticleDraw::Mesh));
    emitter.mesh_scale = {1.0f, 1.0f, 1.0f};
    emitter.size_curve = engine::render::Curve<float>::linear(4.0f, 0.0f);
    emitter.size_curve_xy = engine::render::Curve<glm::vec2>::linear({10.0f, 10.0f}, {10.0f, 10.0f});
    push_particle(emitter, {0.0f, 0.0f, 0.0f});
    emitter.particles[0].age = 0.5f;
    emitter.particles[0].lifetime = 1.0f;
    emitter.particles[0].size = {9.0f, 9.0f};

    world.run(engine::ecs::Schedule::Frame);

    const auto draws = scene_draws(commands);
    ASSERT_EQ(draws.size(), 1u);
    ASSERT_NE(draws[0].particles, nullptr);
    ASSERT_EQ(draws[0].particles->mesh_instances.size(), 1u);
    EXPECT_EQ(draws[0].particles->mesh_instances[0].scale, (glm::vec3{2.0f, 2.0f, 2.0f}));
}

TEST(RenderSystem, OpaqueMeshKeepsSpawnOrderInTheOpaqueQueue) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_perspective_camera(world);
    world.ctx<engine::Time>().delta_time = 0.0f;

    const auto opaque = std::make_shared<StateMaterial>(engine::render::RenderState{
            .blend = engine::render::BlendMode::Opaque,
            .depth_test = true,
            .depth_write = true,
            .cull = engine::render::CullMode::Back,
    });
    const auto glass = std::make_shared<StateMaterial>(engine::render::RenderState{
            .blend = engine::render::BlendMode::Alpha,
            .depth_test = true,
    });
    const auto between = std::make_shared<FakeMesh>();
    const auto glass_mesh = std::make_shared<FakeMesh>();
    engine::render::ParticleEmitter& far_cloud = world.get<engine::render::ParticleEmitter>(
            spawn_emitter(world, opaque, engine::render::ParticleDraw::Mesh));
    push_particle(far_cloud, {0.0f, 0.0f, 2.0f});
    push_particle(far_cloud, {0.0f, 0.0f, -6.0f});
    spawn_mesh_at(world, between, opaque, -2.0f);
    engine::render::ParticleEmitter& near_cloud = world.get<engine::render::ParticleEmitter>(
            spawn_emitter(world, opaque, engine::render::ParticleDraw::Mesh));
    push_particle(near_cloud, {0.0f, 0.0f, 8.0f});
    spawn_mesh_at(world, glass_mesh, glass, 0.0f);

    world.run(engine::ecs::Schedule::Frame);

    const auto draws = scene_draws(commands);
    ASSERT_EQ(draws.size(), 4u);
    ASSERT_NE(draws[0].particles, nullptr);
    EXPECT_EQ(draws[0].particles->mesh_instances[0].position, (glm::vec3{0.0f, 0.0f, 8.0f}));
    EXPECT_EQ(draws[1].mesh, between);
    ASSERT_NE(draws[2].particles, nullptr);
    ASSERT_EQ(draws[2].particles->mesh_instances.size(), 2u);
    EXPECT_EQ(draws[2].particles->mesh_instances[0].position, (glm::vec3{0.0f, 0.0f, 2.0f}));
    EXPECT_EQ(draws[2].particles->mesh_instances[1].position, (glm::vec3{0.0f, 0.0f, -6.0f}));
    EXPECT_EQ(draws[3].mesh, glass_mesh);
}

TEST(RenderSystem, MeshShaderWithoutInstanceInputsIsSkipped) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_perspective_camera(world);
    world.ctx<engine::Time>().delta_time = 0.0f;

    const auto material = std::make_shared<ShaderStateMaterial>(std::make_shared<DeclaresShader>(false),
            engine::render::RenderState{.blend = engine::render::BlendMode::Alpha, .depth_test = true});
    engine::render::ParticleEmitter& emitter = world.get<engine::render::ParticleEmitter>(
            spawn_emitter(world, material, engine::render::ParticleDraw::Mesh));
    push_particle(emitter, {0.0f, 0.0f, 0.0f});

    world.run(engine::ecs::Schedule::Frame);
    world.run(engine::ecs::Schedule::Frame);

    for (const SceneDraw& draw : scene_draws(commands)) {
        EXPECT_EQ(draw.particles, nullptr);
    }
}

TEST(RenderSystem, LocalBillboardDepthSortUsesWorldPosition) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_perspective_camera(world);
    world.ctx<engine::Time>().delta_time = 0.0f;

    const auto glass = std::make_shared<StateMaterial>(engine::render::RenderState{
            .blend = engine::render::BlendMode::Alpha,
            .depth_test = true,
    });
    const auto glass_mesh = std::make_shared<FakeMesh>();
    spawn_mesh_at(world, glass_mesh, glass, 1.0f);

    const engine::ecs::Entity entity = spawn_emitter(world, std::make_shared<StateMaterial>(engine::render::RenderState{
            .blend = engine::render::BlendMode::Alpha,
            .depth_test = true,
            .cull = engine::render::CullMode::Back,
    }), engine::render::ParticleDraw::Billboard);
    engine::Transform transform{};
    transform.position = {0.0f, 0.0f, 3.0f};
    world.emplace<engine::Transform>(entity, transform);
    engine::render::ParticleEmitter& emitter = world.get<engine::render::ParticleEmitter>(entity);
    emitter.simulation_space = engine::render::SimulationSpace::Local;
    push_particle(emitter, {0.0f, 0.0f, -1.0f});
    push_particle(emitter, {0.0f, 0.0f, 2.0f});

    world.run(engine::ecs::Schedule::Frame);

    const auto draws = scene_draws(commands);
    ASSERT_EQ(draws.size(), 2u);
    EXPECT_EQ(draws[0].mesh, glass_mesh);
    ASSERT_NE(draws[1].particles, nullptr);
    ASSERT_EQ(draws[1].particles->instances.size(), 2u);
    EXPECT_EQ(draws[1].particles->instances[0].position, (glm::vec3{0.0f, 0.0f, 2.0f}));
    EXPECT_EQ(draws[1].particles->instances[1].position, (glm::vec3{0.0f, 0.0f, 5.0f}));
}

// The backend reads depth, cull and blend from the command's material.
TEST(RenderSystem, DrawMeshCarriesTheMaterialWithItsRenderState) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_perspective_camera(world);

    const engine::render::RenderState state{
            .blend = engine::render::BlendMode::Opaque,
            .depth_test = true,
            .depth_write = true,
            .cull = engine::render::CullMode::Front,
    };
    const auto material = std::make_shared<StateMaterial>(state);
    spawn_mesh_at(world, std::make_shared<FakeMesh>(), material, 0.0f);

    world.run(engine::ecs::Schedule::Frame);

    ASSERT_EQ(commands.size(), 2u);
    const auto& cmd = std::get<engine::render::CmdDrawMesh>(commands[1]);
    ASSERT_EQ(cmd.material, material);
    EXPECT_EQ(cmd.material->render_state(), state);
}

TEST(RenderSystem, RenderWorldViewportPushesSetViewWithViewportAndClearColorAndDrawsMeshes) {
    engine::ecs::World scene;
    const auto mesh_high = std::make_shared<FakeMesh>();
    const auto mesh_low = std::make_shared<FakeMesh>();
    const auto material = std::make_shared<FakeMaterial>();
    spawn_mesh_at(scene, mesh_high, material, 0.0f, 5);
    spawn_mesh_at(scene, mesh_low, material, 0.0f, 1);

    engine::Camera camera{};
    camera.projection = engine::Projection::Perspective;
    camera.fov_y = glm::radians(60.0f);
    camera.near_clip = 0.1f;
    camera.far_clip = 100.0f;
    engine::Transform camera_transform{};
    camera_transform.position = {1.0f, 2.0f, 8.0f};
    const glm::mat4 camera_world = engine::model_matrix(camera_transform);
    const engine::render::Rect viewport{24.0f, 48.0f, 400.0f, 200.0f};
    const glm::vec4 clear_color{0.08f, 0.09f, 0.12f, 1.0f};

    engine::render::CommandBuffer commands;
    engine::render_world_viewport(scene, engine::WorldViewportRender{
            .commands = &commands,
            .viewport = viewport,
            .clear_color = clear_color,
            .camera = camera,
            .camera_world = camera_world,
    });

    ASSERT_EQ(commands.size(), 3u);
    const auto* view = std::get_if<engine::render::CmdSetView>(&commands[0]);
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->viewport, viewport);
    EXPECT_EQ(view->clear_color, clear_color);
    EXPECT_EQ(view->camera_position, camera_transform.position);
    expect_mat4_eq(view->view, engine::view_matrix(camera_world));
    expect_mat4_eq(view->projection, engine::projection_matrix(camera, engine::ui::WindowSize{400, 200}));
    EXPECT_EQ(std::get<engine::render::CmdDrawMesh>(commands[1]).mesh, mesh_low);
    EXPECT_EQ(std::get<engine::render::CmdDrawMesh>(commands[2]).mesh, mesh_high);
}

TEST(RenderSystem, RenderWorldViewportClearsEvenWhenSceneIsEmptyAndSkipsDegenerateRect) {
    engine::ecs::World empty_scene;
    engine::render::CommandBuffer commands;
    const engine::render::Rect viewport{0.0f, 0.0f, 160.0f, 90.0f};
    const glm::vec4 clear_color{0.1f, 0.1f, 0.1f, 1.0f};

    engine::render_world_viewport(empty_scene, engine::WorldViewportRender{
            .commands = &commands,
            .viewport = {0.0f, 0.0f, 0.0f, 90.0f},
            .clear_color = clear_color,
    });
    EXPECT_TRUE(commands.empty());

    engine::render_world_viewport(empty_scene, engine::WorldViewportRender{
            .commands = &commands,
            .viewport = viewport,
            .clear_color = clear_color,
    });
    ASSERT_EQ(commands.size(), 1u);
    const auto* view = std::get_if<engine::render::CmdSetView>(&commands[0]);
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->viewport, viewport);
    EXPECT_EQ(view->clear_color, clear_color);
}


namespace {

// The camera of `spawn_camera` sees x in [-40/3, 40/3] and y in [-10, 10] (ortho_size 10 on 800 x 600) and z in
// [-1, 1].
engine::ecs::Entity spawn_renderable_at(engine::ecs::World& world, std::shared_ptr<engine::render::IMesh> mesh,
        std::shared_ptr<engine::render::IMaterial> material, glm::vec3 position) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
    world.emplace<engine::render::Renderable>(entity, make_renderable(std::move(mesh), std::move(material), 0));
    return entity;
}

engine::ecs::Entity spawn_sized_sprite_at(engine::ecs::World& world, std::shared_ptr<engine::render::IMesh> mesh,
        glm::vec3 position, glm::vec2 pixel_size, glm::vec2 pivot) {
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::Transform>(entity, engine::Transform{.position = position});
    engine::render::Sprite sprite{};
    sprite.mesh = std::move(mesh);
    sprite.material = std::make_shared<FakeMaterial>();
    sprite.pixel_size = pixel_size;
    sprite.pixels_per_unit = 100.0f;
    sprite.pivot = pivot;
    world.emplace<engine::render::Sprite>(entity, std::move(sprite));
    return entity;
}

}

TEST(RenderSystem, CulledRenderablePushesNoDrawMesh) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_camera(world);
    const auto material = std::make_shared<FakeMaterial>();
    const auto hidden = std::make_shared<FakeMesh>();
    spawn_renderable_at(world, hidden, material, {100.0f, 0.0f, 0.0f});

    world.run(engine::ecs::Schedule::Frame);
    ASSERT_EQ(commands.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdSetView>(commands[0]));

    const auto seen = std::make_shared<FakeMesh>();
    spawn_renderable_at(world, seen, material, {0.0f, 0.0f, 0.0f});
    world.run(engine::ecs::Schedule::Frame);
    EXPECT_EQ(drawn_meshes(commands), (std::vector<std::shared_ptr<engine::render::IMesh>>{seen}));
}

TEST(RenderSystem, RenderableStraddlingTheEdgeIsDrawn) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_camera(world);
    const auto material = std::make_shared<FakeMaterial>();
    const auto straddling = std::make_shared<FakeMesh>();
    const auto beyond = std::make_shared<FakeMesh>();
    // The right edge is at 13.33: the unit quad at 13.6 reaches back to 13.1, the one at 14 starts at 13.5.
    spawn_renderable_at(world, straddling, material, {13.6f, 0.0f, 0.0f});
    spawn_renderable_at(world, beyond, material, {14.0f, 0.0f, 0.0f});

    world.run(engine::ecs::Schedule::Frame);

    EXPECT_EQ(drawn_meshes(commands), (std::vector<std::shared_ptr<engine::render::IMesh>>{straddling}));
}

TEST(RenderSystem, CullingUsesTheWorldTransformOfAChild) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_camera(world);
    const auto material = std::make_shared<FakeMaterial>();
    const engine::ecs::Entity parent = world.create();
    world.emplace<engine::Transform>(parent, engine::Transform{.position = {100.0f, 0.0f, 0.0f}});
    const auto back_home = std::make_shared<FakeMesh>();
    const auto with_parent = std::make_shared<FakeMesh>();
    ASSERT_TRUE(engine::ecs::set_parent(
            world, spawn_renderable_at(world, back_home, material, {-100.0f, 0.0f, 0.0f}), parent));
    ASSERT_TRUE(engine::ecs::set_parent(
            world, spawn_renderable_at(world, with_parent, material, {0.0f, 0.0f, 0.0f}), parent));

    world.run(engine::ecs::Schedule::Frame);

    EXPECT_EQ(drawn_meshes(commands), (std::vector<std::shared_ptr<engine::render::IMesh>>{back_home}));
}

TEST(RenderSystem, SpriteCullingCountsPixelSizeAndPivot) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_camera(world);
    const auto long_from_left = std::make_shared<FakeMesh>();
    const auto small = std::make_shared<FakeMesh>();
    const auto long_from_right = std::make_shared<FakeMesh>();
    // 20 x 1 world units with the pivot on the left edge: x in [-25, -5], inside the view's left edge at -13.33.
    spawn_sized_sprite_at(world, long_from_left, {-25.0f, 0.0f, 0.0f}, {2000.0f, 100.0f}, {0.0f, 0.5f});
    // 1 x 1: x in [-25, -24].
    spawn_sized_sprite_at(world, small, {-25.0f, 0.0f, 0.0f}, {100.0f, 100.0f}, {0.0f, 0.5f});
    // 20 x 1 with the pivot on the right edge: x in [-45, -25].
    spawn_sized_sprite_at(world, long_from_right, {-25.0f, 0.0f, 0.0f}, {2000.0f, 100.0f}, {1.0f, 0.5f});

    world.run(engine::ecs::Schedule::Frame);

    EXPECT_EQ(drawn_meshes(commands), (std::vector<std::shared_ptr<engine::render::IMesh>>{long_from_left}));
}

TEST(RenderSystem, FlippedSpriteIsNotCulled) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_camera(world);
    const auto mesh = std::make_shared<FakeMesh>();
    const engine::ecs::Entity entity =
            spawn_sized_sprite_at(world, mesh, {-25.0f, 0.0f, 0.0f}, {2000.0f, 100.0f}, {0.0f, 0.5f});
    engine::render::Sprite& sprite = world.get<engine::render::Sprite>(entity);
    sprite.flip_x = true;
    sprite.flip_y = true;

    world.run(engine::ecs::Schedule::Frame);

    EXPECT_EQ(drawn_meshes(commands).size(), 1u);
}

TEST(RenderSystem, EntityVisibleInAnyBoundWindowIsDrawnInEvery) {
    engine::render::CommandBuffer primary;
    engine::render::CommandBuffer wide;
    const engine::WindowId wide_window{2};
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{
            .commands_for_window = [&](engine::WindowId id) -> engine::render::CommandBuffer* {
                if (id == engine::kPrimaryWindow) {
                    return &primary;
                }
                return id == wide_window ? &wide : nullptr;
            },
    });
    spawn_camera(world);
    // 1600 x 600 sees x in [-26.67, 26.67]; the primary 800 x 600 only [-13.33, 13.33].
    engine::ui::presentation_of(world).sizes.sizes[wide_window] = engine::ui::WindowSize{1600, 600};
    world.ctx<engine::BoundWindows>().ids = {engine::kPrimaryWindow, wide_window};
    const auto material = std::make_shared<FakeMaterial>();
    const auto only_wide = std::make_shared<FakeMesh>();
    const auto neither = std::make_shared<FakeMesh>();
    spawn_renderable_at(world, only_wide, material, {20.0f, 0.0f, 0.0f});
    spawn_renderable_at(world, neither, material, {40.0f, 0.0f, 0.0f});

    world.run(engine::ecs::Schedule::Frame);

    const std::vector<std::shared_ptr<engine::render::IMesh>> expected{only_wide};
    EXPECT_EQ(drawn_meshes(primary), expected);
    EXPECT_EQ(drawn_meshes(wide), expected);
}

TEST(RenderSystem, FrustumCullFalseDrawsOffscreen) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_camera(world);
    const auto renderable_mesh = std::make_shared<FakeMesh>();
    const auto sprite_mesh = std::make_shared<FakeMesh>();
    const engine::ecs::Entity renderable =
            spawn_renderable_at(world, renderable_mesh, std::make_shared<FakeMaterial>(), {100.0f, 0.0f, 0.0f});
    world.get<engine::render::Renderable>(renderable).frustum_cull = false;
    const engine::ecs::Entity sprite =
            spawn_sized_sprite_at(world, sprite_mesh, {-100.0f, 0.0f, 0.0f}, {100.0f, 100.0f}, {0.5f, 0.5f});
    world.get<engine::render::Sprite>(sprite).frustum_cull = false;

    world.run(engine::ecs::Schedule::Frame);

    EXPECT_EQ(drawn_meshes(commands).size(), 2u);
}

TEST(RenderSystem, PerspectiveCameraCullsWhatIsBehindIt) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_perspective_camera(world);
    const auto material = std::make_shared<FakeMaterial>();
    const auto in_front = std::make_shared<FakeMesh>(engine::render::make_cube());
    const auto behind = std::make_shared<FakeMesh>(engine::render::make_cube());
    spawn_mesh_at(world, in_front, material, 0.0f);
    spawn_mesh_at(world, behind, material, 20.0f);

    world.run(engine::ecs::Schedule::Frame);

    EXPECT_EQ(drawn_meshes(commands), (std::vector<std::shared_ptr<engine::render::IMesh>>{in_front}));
}

TEST(RenderSystem, UpdatedMeshBoundsChangeCulling) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_camera(world);
    const auto mesh = std::make_shared<FakeMesh>();
    spawn_renderable_at(world, mesh, std::make_shared<FakeMaterial>(), {100.0f, 0.0f, 0.0f});

    world.run(engine::ecs::Schedule::Frame);
    EXPECT_TRUE(drawn_meshes(commands).empty());

    // The same mesh now has its vertices 100 units to the left, back under the camera.
    engine::render::MeshDesc moved = engine::render::make_quad();
    for (engine::render::MeshVertex& vertex : moved.vertices) {
        vertex.position.x -= 100.0f;
    }
    ASSERT_TRUE(mesh->update(moved));
    world.run(engine::ecs::Schedule::Frame);
    EXPECT_EQ(drawn_meshes(commands).size(), 1u);
}

TEST(RenderSystem, OffscreenRenderableWithoutMaterialIsStillFatal) {
    engine::render::CommandBuffer commands;
    fakes::RecordingFatal fatal;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands, .fatal = &fatal});
    spawn_camera(world);
    spawn_renderable_at(world, std::make_shared<FakeMesh>(), nullptr, {100.0f, 0.0f, 0.0f});

    EXPECT_THROW(world.run(engine::ecs::Schedule::Frame), fakes::FatalReport);
    EXPECT_EQ(fatal.call_count, 1);
}

namespace {

const engine::render::CmdDrawParticles* particle_command_at(const engine::render::CommandBuffer& commands,
        glm::vec3 position) {
    for (std::size_t i = 0; i < commands.size(); ++i) {
        const auto* cmd = std::get_if<engine::render::CmdDrawParticles>(&commands[i]);
        if (cmd == nullptr) {
            continue;
        }
        for (const engine::render::ParticleInstance& instance : cmd->instances) {
            if (instance.position == position) {
                return cmd;
            }
        }
        for (const engine::render::MeshParticleInstance& instance : cmd->mesh_instances) {
            if (instance.position == position) {
                return cmd;
            }
        }
    }
    return nullptr;
}

int particle_command_count(const engine::render::CommandBuffer& commands) {
    int count = 0;
    for (std::size_t i = 0; i < commands.size(); ++i) {
        if (std::holds_alternative<engine::render::CmdDrawParticles>(commands[i])) {
            ++count;
        }
    }
    return count;
}

// make_cube(1) is [-0.5, 0.5]. Shifted by -1.5 on X the local box is X [-2, -1], not centered on the origin.
std::shared_ptr<FakeMesh> off_center_cube() {
    engine::render::MeshDesc cube = engine::render::make_cube(1.0f);
    for (engine::render::MeshVertex& vertex : cube.vertices) {
        vertex.position.x -= 1.5f;
    }
    return std::make_shared<FakeMesh>(std::move(cube));
}

}

// spawn_perspective_camera at z = 10, 60° vertical fov, 800 x 600: at z = 0, x is about ±7.698.
TEST(RenderSystem, ParticleEmitterOutsideEveryFrustumIsDroppedAndAStraddleIsKept) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_perspective_camera(world);
    world.ctx<engine::Time>().delta_time = 0.0f;
    const auto material = std::make_shared<FakeMaterial>();

    engine::render::ParticleEmitter& outside = world.get<engine::render::ParticleEmitter>(
            spawn_emitter(world, material, engine::render::ParticleDraw::Plane));
    push_particle(outside, {14.0f, 0.0f, 0.0f});
    outside.particles.back().size = {4.0f, 1.0f};

    const engine::ecs::Entity straddle_entity =
            spawn_emitter(world, material, engine::render::ParticleDraw::Plane);
    world.emplace<engine::Transform>(straddle_entity, engine::Transform{.position = {20.0f, 0.0f, 0.0f}});
    engine::render::ParticleEmitter& straddle = world.get<engine::render::ParticleEmitter>(straddle_entity);
    straddle.simulation_space = engine::render::SimulationSpace::Local;
    push_particle(straddle, {-11.0f, 0.0f, 0.0f});
    straddle.particles.back().size = {4.0f, 1.0f};

    engine::render::ParticleEmitter& always = world.get<engine::render::ParticleEmitter>(
            spawn_emitter(world, material, engine::render::ParticleDraw::Plane));
    always.frustum_cull = false;
    push_particle(always, {18.0f, 0.0f, 0.0f});
    always.particles.back().size = {4.0f, 1.0f};

    spawn_emitter(world, material, engine::render::ParticleDraw::Plane);

    world.run(engine::ecs::Schedule::Frame);

    EXPECT_EQ(particle_command_count(commands), 2);
    EXPECT_EQ(particle_command_at(commands, {14.0f, 0.0f, 0.0f}), nullptr);
    const engine::render::CmdDrawParticles* kept = particle_command_at(commands, {9.0f, 0.0f, 0.0f});
    ASSERT_NE(kept, nullptr);
    ASSERT_EQ(kept->instances.size(), 1u);
    EXPECT_EQ(kept->instances[0].position, (glm::vec3{9.0f, 0.0f, 0.0f}));
    EXPECT_NE(particle_command_at(commands, {18.0f, 0.0f, 0.0f}), nullptr);
}

// Camera moved to x = 20. At z = 0 the view is about x in [12.30, 27.70], so the origin is outside.
// Local X [-2, -1] times instance scale -10 (mesh_scale.x -5 times size factor 2) is world X [10, 20].
TEST(RenderSystem, MeshParticleScaleKeepsAnOffCenterCornerInsideTheFrustum) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_perspective_camera(world);
    world.get<engine::Transform>(world.ctx<engine::ActiveCamera>().entity).position = {20.0f, 0.0f, 10.0f};
    world.ctx<engine::Time>().delta_time = 0.0f;
    const auto material = std::make_shared<FakeMaterial>();

    const engine::ecs::Entity kept_entity = spawn_emitter(world, material, engine::render::ParticleDraw::Mesh);
    engine::render::ParticleEmitter& kept = world.get<engine::render::ParticleEmitter>(kept_entity);
    kept.mesh = off_center_cube();
    kept.mesh_scale = {-5.0f, 1.0f, 1.0f};
    kept.size_curve = engine::render::Curve<float>::constant(2.0f);
    push_particle(kept, {0.0f, 0.0f, 0.0f});

    const engine::ecs::Entity dropped_entity = spawn_emitter(world, material, engine::render::ParticleDraw::Mesh);
    engine::render::ParticleEmitter& dropped = world.get<engine::render::ParticleEmitter>(dropped_entity);
    dropped.mesh = off_center_cube();
    push_particle(dropped, {0.0f, 0.0f, 0.0f});

    world.run(engine::ecs::Schedule::Frame);

    EXPECT_EQ(particle_command_count(commands), 1);
    const engine::render::CmdDrawParticles* cmd = particle_command_at(commands, {0.0f, 0.0f, 0.0f});
    ASSERT_NE(cmd, nullptr);
    ASSERT_EQ(cmd->mesh_instances.size(), 1u);
    EXPECT_EQ(cmd->mesh_instances[0].position, (glm::vec3{0.0f, 0.0f, 0.0f}));
    EXPECT_EQ(cmd->mesh_instances[0].scale, (glm::vec3{-10.0f, 2.0f, 2.0f}));
}

TEST(RenderSystem, RenderWorldViewportCullsWithItsCamera) {
    engine::ecs::World scene;
    const auto material = std::make_shared<FakeMaterial>();
    const auto seen = std::make_shared<FakeMesh>();
    const auto hidden = std::make_shared<FakeMesh>();
    spawn_renderable_at(scene, seen, material, {0.0f, 0.0f, 0.0f});
    spawn_renderable_at(scene, hidden, material, {100.0f, 0.0f, 0.0f});

    engine::Camera camera{};
    camera.near_clip = -10.0f;
    camera.far_clip = 10.0f;
    engine::render::CommandBuffer commands;
    engine::render_world_viewport(scene, engine::WorldViewportRender{
            .commands = &commands,
            .viewport = {0.0f, 0.0f, 400.0f, 200.0f},
            .camera = camera,
            .camera_world = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, 5.0f}),
    });

    ASSERT_FALSE(commands.empty());
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdSetView>(commands[0]));
    EXPECT_EQ(drawn_meshes(commands), (std::vector<std::shared_ptr<engine::render::IMesh>>{seen}));

    // The camera moved over the other one: now only it is drawn.
    commands.clear();
    engine::render_world_viewport(scene, engine::WorldViewportRender{
            .commands = &commands,
            .viewport = {0.0f, 0.0f, 400.0f, 200.0f},
            .camera = camera,
            .camera_world = glm::translate(glm::mat4{1.0f}, glm::vec3{100.0f, 0.0f, 5.0f}),
    });
    EXPECT_EQ(drawn_meshes(commands), (std::vector<std::shared_ptr<engine::render::IMesh>>{hidden}));
}

// LQ-54: FluidRender2D draws a fluid as one batch of soft cards (the surface: tests/fluid_surface_test.cpp). With no
// assets the composite shader is missing, so the default surface look falls back to these cards.

namespace {

engine::ecs::Entity spawn_fluid_look(engine::ecs::World& world, std::shared_ptr<engine::render::IMaterial> material,
        glm::vec2 at) {
    const engine::ecs::Entity entity = world.create();
    engine::Fluid2D& fluid = world.emplace<engine::Fluid2D>(entity, engine::Fluid2DParams{.radius = 0.05f});
    (void)fluid.spawn(at, {0.0f, 0.0f});
    (void)fluid.spawn(at + glm::vec2{0.1f, 0.0f}, {1.0f, 0.0f});
    (void)fluid.spawn(at + glm::vec2{0.2f, 0.0f}, {0.0f, -4.0f});
    world.emplace<engine::render::FluidRender2D>(entity, engine::render::FluidRender2D{
            .material = std::move(material),
            .mesh = std::make_shared<FakeMesh>(),
            .color = {0.0f, 0.0f, 1.0f, 1.0f},
            .foam_color = {1.0f, 1.0f, 1.0f, 1.0f},
            .foam_speed = 2.0f,
            .size = 4.0f,
            .layer = 3,
    });
    return entity;
}

}

TEST(RenderSystem, FluidRenderDrawsOneCardPerParticleWithFoamBySpeed) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_perspective_camera(world);
    const auto material = std::make_shared<FakeMaterial>();
    (void)spawn_fluid_look(world, material, {1.0f, 1.0f});
    world.run(engine::ecs::Schedule::Frame);

    ASSERT_EQ(particle_command_count(commands), 1);
    const engine::render::CmdDrawParticles* cards = particle_command_at(commands, {1.0f, 1.0f, 0.0f});
    ASSERT_NE(cards, nullptr);
    EXPECT_EQ(cards->material, material);
    EXPECT_EQ(cards->draw, engine::render::ParticleDraw::Plane);
    ASSERT_EQ(cards->instances.size(), 3u);
    for (const engine::render::ParticleInstance& card : cards->instances) {
        EXPECT_FLOAT_EQ(card.size.x, 0.2f) << "4 radii wide";
    }
    EXPECT_EQ(cards->instances[0].color, (glm::vec4{0.0f, 0.0f, 1.0f, 1.0f})) << "at rest: the water color";
    EXPECT_EQ(cards->instances[1].color, (glm::vec4{0.5f, 0.5f, 1.0f, 1.0f})) << "half the foam speed";
    EXPECT_EQ(cards->instances[2].color, (glm::vec4{1.0f, 1.0f, 1.0f, 1.0f})) << "at or past it: foam";
    EXPECT_EQ(cards->instances[2].velocity, (glm::vec3{0.0f, -4.0f, 0.0f}));
}

TEST(RenderSystem, FluidRenderOutsideEveryFrustumIsDropped) {
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    spawn_perspective_camera(world);
    const auto material = std::make_shared<FakeMaterial>();
    (void)spawn_fluid_look(world, material, {50.0f, 0.0f});
    const engine::ecs::Entity always = spawn_fluid_look(world, material, {60.0f, 0.0f});
    world.get<engine::render::FluidRender2D>(always).frustum_cull = false;
    world.run(engine::ecs::Schedule::Frame);
    EXPECT_EQ(particle_command_count(commands), 1);
    EXPECT_EQ(particle_command_at(commands, {50.0f, 0.0f, 0.0f}), nullptr);
    EXPECT_NE(particle_command_at(commands, {60.0f, 0.0f, 0.0f}), nullptr);
}

TEST(RenderSystem, FluidRenderRoundTripsThroughTheSerializer) {
    engine::ecs::World world;
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::render::FluidRender2D>(entity, engine::render::FluidRender2D{
            .look = engine::render::FluidRender2D::Look::Cards,
            .color = {0.1f, 0.2f, 0.3f, 0.4f},
            .foam_speed = 3.0f,
            .size = 5.0f,
            .z = -1.0f,
            .blend = engine::render::BlendMode::Additive,
            .layer = 7,
            .order_in_layer = 2,
            .frustum_cull = false,
            .splat_weight = 0.4f,
            .threshold = 0.25f,
            .edge = 0.1f,
            .refraction = 3.0f,
            .foam_scale = 1.5f,
    });
    const std::string saved = engine::ecs::save_world_to_toml(world);
    engine::ecs::World loaded;
    ASSERT_TRUE(engine::ecs::load_world_from_toml(loaded, saved).has_value());
    const engine::render::FluidRender2D* look = nullptr;
    loaded.view<engine::render::FluidRender2D>().each([&](const engine::render::FluidRender2D& f) { look = &f; });
    ASSERT_NE(look, nullptr);
    EXPECT_EQ(look->color, (glm::vec4{0.1f, 0.2f, 0.3f, 0.4f}));
    EXPECT_FLOAT_EQ(look->foam_speed, 3.0f);
    EXPECT_FLOAT_EQ(look->size, 5.0f);
    EXPECT_FLOAT_EQ(look->z, -1.0f);
    EXPECT_EQ(look->blend, engine::render::BlendMode::Additive);
    EXPECT_EQ(look->layer, 7);
    EXPECT_EQ(look->order_in_layer, 2);
    EXPECT_FALSE(look->frustum_cull);
    EXPECT_EQ(look->look, engine::render::FluidRender2D::Look::Cards);
    EXPECT_FLOAT_EQ(look->splat_weight, 0.4f);
    EXPECT_FLOAT_EQ(look->threshold, 0.25f);
    EXPECT_FLOAT_EQ(look->edge, 0.1f);
    EXPECT_FLOAT_EQ(look->refraction, 3.0f);
    EXPECT_FLOAT_EQ(look->foam_scale, 1.5f);
}
