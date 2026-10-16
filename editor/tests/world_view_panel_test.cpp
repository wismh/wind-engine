#include <gtest/gtest.h>

#include "edit_module.h"
#include "editor_selection.h"
#include "world_view_panel.h"

#include "fixtures/game_module/fixture_log.h"

#include <engine/core/input_system.h>
#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_serializer.h>
#include <engine/render/command_buffer.h>
#include <engine/render/commands.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/presentation.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

namespace {

using engine::render::Rect;

constexpr engine::WindowId kEditorWindow{1};
constexpr Rect kCanvasRect{40.0f, 80.0f, 400.0f, 236.0f};
constexpr Rect kViewportRect{40.0f, 80.0f + editor::WorldViewPanel::kHeaderHeight, 400.0f, 200.0f};
const std::filesystem::path kFixture{WIND_FIXTURE_GAME};

class StubPlayHost final : public editor::IPlayHost {
public:
    std::expected<void, std::string> load_catalog(const std::filesystem::path&) override { return {}; }
    void unload_catalog(const std::filesystem::path&) override {}
    void attach(engine::IGame&) override {}
    void detach() override {}
    void attach_tools(engine::ecs::World&) override {}
    void detach_tools() override {}
};

class WorldViewPanelTest : public ::testing::Test {
protected:
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "wind_world_view_panel_test" /
            ::testing::UnitTest::GetInstance()->current_test_info()->name();
    editor::EditorSelection selection;
    editor::WorldViewPanel panel{selection};
    engine::ecs::World editor_world;
    engine::ecs::Entity canvas{};

    void SetUp() override {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        editor_world.ctx<engine::Time>().delta_time = 0.1f;

        engine::ui::UiCanvas ui_canvas;
        ui_canvas.window = kEditorWindow;
        ui_canvas.fit = engine::ui::UiFit::Fixed;
        ui_canvas.rect = kCanvasRect;
        ui_canvas.data_context = panel.view_model();
        canvas = editor_world.create();
        editor_world.emplace<engine::ui::UiCanvas>(canvas, std::move(ui_canvas));
    }

    void TearDown() override {
        panel.close();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    std::filesystem::path write_world(std::string_view name, std::string_view content) const {
        const std::filesystem::path path = dir / name;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << content;
        return path;
    }
};

TEST_F(WorldViewPanelTest, StartsEmptyAndSyncSelectionOpensWorldFileAndIgnoresOtherSelections) {
    const editor::WorldViewViewModel& vm = *panel.view_model();
    EXPECT_FALSE(panel.world_loaded());
    EXPECT_EQ(vm.statusText.get(), "No world open.");
    EXPECT_EQ(vm.emptyText.get(), "Select a .world file in the Project tab.");
    EXPECT_EQ(vm.viewportBg.get(), "#1e1f22");

    const std::filesystem::path other = write_world("wind_project.toml", "name = \"demo\"\n");
    selection.select(editor::AssetSelection{.key = "wind_project.toml", .path = other});
    EXPECT_FALSE(panel.sync_selection());
    EXPECT_FALSE(panel.world_loaded());

    const std::filesystem::path level = write_world("assets/scenes/level.world", R"(
version = 1

[[entities]]
id = "hero"
transform = { position = [2.0, 3.0, 0.0] }

[[entities]]
id = "prop"
transform = { position = [-1.0, 0.0, 0.0] }
)");
    selection.select(editor::AssetSelection{.key = "assets/scenes/level.world", .path = level});
    EXPECT_TRUE(panel.sync_selection());
    EXPECT_FALSE(panel.sync_selection()) << "same selection revision is not opened twice";

    EXPECT_TRUE(panel.world_loaded());
    EXPECT_EQ(panel.open_path(), level);
    EXPECT_EQ(vm.statusText.get(), "assets/scenes/level.world  (2 entities)");
    EXPECT_EQ(vm.projectionLabel.get(), "2D");
    EXPECT_EQ(vm.viewportBg.get(), "#00000000");
    EXPECT_TRUE(vm.emptyText.get().empty());

    // Selecting a non-world file afterwards keeps the open world in the World panel.
    selection.select(editor::AssetSelection{.key = "wind_project.toml", .path = other});
    EXPECT_FALSE(panel.sync_selection());
    EXPECT_TRUE(panel.world_loaded());
    EXPECT_EQ(panel.open_path(), level);
}

TEST_F(WorldViewPanelTest, InvalidWorldFileShowsErrorState) {
    const std::filesystem::path broken = write_world("broken.world", "version = [invalid\n");
    EXPECT_FALSE(panel.open_world(broken));
    const editor::WorldViewViewModel& vm = *panel.view_model();
    EXPECT_FALSE(panel.world_loaded());
    EXPECT_EQ(vm.statusText.get(), "broken.world");
    EXPECT_EQ(vm.viewportBg.get(), "#1e1f22");
    EXPECT_FALSE(vm.emptyText.get().empty());
}

TEST_F(WorldViewPanelTest, PerspectiveSceneCameraStartsIn3DAndCommandsToggleAndResetCamera) {
    const std::filesystem::path scene_3d = write_world("arena.world", R"(
version = 1
active_camera = "cam"

[[entities]]
id = "cam"
transform = { position = [4.0, 5.0, 12.0] }
camera = { projection = "perspective", fov_y = 1.0, near_clip = 0.2, far_clip = 250.0 }
)");
    ASSERT_TRUE(panel.open_world(scene_3d));
    editor::WorldViewViewModel& vm = *panel.view_model();
    EXPECT_EQ(vm.projectionLabel.get(), "3D");
    EXPECT_EQ(panel.camera().projection, engine::Projection::Perspective);
    EXPECT_FLOAT_EQ(panel.camera().fov_y, 1.0f);
    EXPECT_EQ(panel.camera_position(), (glm::vec3{4.0f, 5.0f, 12.0f}));

    // Dolly via wheel, then reset via command.
    engine::ui::pointer_for(editor_world, kEditorWindow).position = {100.0f, 150.0f};
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Wheel,
            .position = {100.0f, 150.0f},
            .wheel_y = 2.0f,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();
    EXPECT_NE(panel.camera_position(), (glm::vec3{4.0f, 5.0f, 12.0f}));

    vm.resetCamera.execute();
    EXPECT_EQ(panel.camera_position(), (glm::vec3{4.0f, 5.0f, 12.0f}));

    vm.toggleProjection.execute();
    EXPECT_EQ(vm.projectionLabel.get(), "2D");
    EXPECT_EQ(panel.camera().projection, engine::Projection::Orthographic);

    vm.toggleProjection.execute();
    EXPECT_EQ(vm.projectionLabel.get(), "3D");
    EXPECT_EQ(panel.camera().projection, engine::Projection::Perspective);
}

TEST_F(WorldViewPanelTest, TwoDimensionalCameraPansOnDragAndZoomsOnWheelInsideViewport) {
    const std::filesystem::path scene_2d = write_world("flat.world", R"(
version = 1
active_camera = "cam"

[[entities]]
id = "cam"
transform = { position = [0.0, 0.0, 10.0] }
camera = { projection = "orthographic", ortho_size = 10.0 }
)");
    ASSERT_TRUE(panel.open_world(scene_2d));
    ASSERT_EQ(panel.view_model()->projectionLabel.get(), "2D");
    EXPECT_FLOAT_EQ(panel.camera().ortho_size, 10.0f);

    // A click outside the viewport does not start a drag.
    engine::ui::pointer_for(editor_world, kEditorWindow).position = {10.0f, 10.0f};
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Down,
            .position = {10.0f, 10.0f},
            .button = engine::MouseButton::Right,
    });
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Move,
            .position = {50.0f, 50.0f},
            .relative = {40.0f, 40.0f},
    });
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Up,
            .position = {50.0f, 50.0f},
            .button = engine::MouseButton::Right,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();
    EXPECT_EQ(panel.camera_position(), (glm::vec3{0.0f, 0.0f, 10.0f}));

    // Right-drag inside the viewport (height = 200, ortho_size = 10 -> 0.1 world units per pixel).
    engine::ui::pointer_for(editor_world, kEditorWindow).position = {100.0f, 150.0f};
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Down,
            .position = {100.0f, 150.0f},
            .button = engine::MouseButton::Right,
    });
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Move,
            .position = {120.0f, 180.0f},
            .relative = {20.0f, 30.0f},
    });
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Up,
            .position = {120.0f, 180.0f},
            .button = engine::MouseButton::Right,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();
    EXPECT_NEAR(panel.camera_position().x, -2.0f, 1e-4f);
    EXPECT_NEAR(panel.camera_position().y, 3.0f, 1e-4f);

    // Wheel zooms in.
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Wheel,
            .position = {120.0f, 180.0f},
            .wheel_y = 1.0f,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();
    EXPECT_LT(panel.camera().ortho_size, 10.0f);
}

TEST_F(WorldViewPanelTest, ThreeDimensionalCameraLooksAndFlysWhileRightMouseHeld) {
    const std::filesystem::path scene_3d = write_world("fly.world", R"(
version = 1
active_camera = "cam"

[[entities]]
id = "cam"
transform = { position = [0.0, 0.0, 10.0] }
camera = { projection = "perspective", fov_y = 1.0 }
)");
    ASSERT_TRUE(panel.open_world(scene_3d));
    engine::ui::pointer_for(editor_world, kEditorWindow).position = {100.0f, 150.0f};

    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Down,
            .position = {100.0f, 150.0f},
            .button = engine::MouseButton::Right,
    });
    engine::ecs::EventWriter<engine::KeyEvent>{editor_world}.send(engine::KeyEvent{
            .window = kEditorWindow,
            .key = engine::KeyCode::W,
            .down = true,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();

    EXPECT_LT(panel.camera_position().z, 10.0f) << "W moves forward along -Z";
}

TEST_F(WorldViewPanelTest, RenderPushesViewportSetViewWhenCanvasHasViewportArea) {
    const std::filesystem::path scene = write_world("scene.world", "version = 1\n");
    ASSERT_TRUE(panel.open_world(scene));

    engine::render::CommandBuffer window_commands;
    const engine::EngineSystemDeps deps{
            .commands_for_window = [&](engine::WindowId id) -> engine::render::CommandBuffer* {
                return id == kEditorWindow ? &window_commands : nullptr;
            },
    };

    panel.render(editor_world, canvas, deps);
    ASSERT_EQ(window_commands.size(), 1u);
    const auto* set_view = std::get_if<engine::render::CmdSetView>(&window_commands[0]);
    ASSERT_NE(set_view, nullptr);
    EXPECT_EQ(set_view->viewport, kViewportRect);
    EXPECT_EQ(set_view->clear_color, editor::WorldViewPanel::kViewportClearColor);
}

TEST_F(WorldViewPanelTest, BeforeModuleUnloadDropsPreviewWorldBeforeDllUnloadsAndReloadRestoresIt) {
    StubPlayHost host;
    editor::EditModule edit{host, dir / "live"};
    panel.set_component_registrar(
            [&edit](engine::ecs::WorldSerializer& s) { edit.register_components(s); });
    ASSERT_TRUE(edit.load(kFixture).has_value());

    const std::filesystem::path scene = write_world("beacon.world", R"(
version = 1

[[entities]]
id = "beacon"
fixture_component = { label = "preview", power = 7 }
)");
    ASSERT_TRUE(panel.open_world(scene));
    ASSERT_EQ(panel.edit_world().alive_entities().size(), 1u);
    const auto* first = panel.edit_world().try_get<fixture::FixtureComponent>(panel.edit_world().alive_entities()[0]);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->label, "preview");
    EXPECT_EQ(first->power, 7);

    panel.before_module_unload();
    EXPECT_EQ(panel.edit_world().alive_entities().size(), 0u);
    EXPECT_EQ(panel.open_path(), scene);
    edit.unload();

    ASSERT_TRUE(edit.load(kFixture).has_value());
    panel.reload();
    ASSERT_EQ(panel.edit_world().alive_entities().size(), 1u);
    const auto* reloaded =
            panel.edit_world().try_get<fixture::FixtureComponent>(panel.edit_world().alive_entities()[0]);
    ASSERT_NE(reloaded, nullptr);
    EXPECT_EQ(reloaded->label, "preview");
    EXPECT_EQ(reloaded->power, 7);

    panel.before_module_unload();
}

}
