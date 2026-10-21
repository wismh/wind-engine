#include <gtest/gtest.h>

#include "edit_module.h"
#include "editor_selection.h"
#include "world_view_panel.h"

#include "fixtures/game_module/fixture_log.h"

#include <engine/core/input_system.h>
#include <engine/core/time.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/events.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_serializer.h>
#include <engine/render/command_buffer.h>
#include <engine/render/commands.h>
#include <engine/render/graphic_factory.h>
#include <engine/resources/assets_db.h>
#include <engine/ui/canvas.h>
#include <engine/ui/cursor.h>
#include <engine/ui/document.h>
#include <engine/ui/presentation.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

namespace {

using engine::render::Rect;
using engine::ui::Cursor;

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
    class Fatal final : public engine::IFatalError {
    public:
        [[noreturn]] void report(std::string_view) override { std::abort(); }
    };
    Fatal fatal;
    engine::AssetsDb assets(fatal);
    StubPlayHost host;
    editor::EditModule edit{host, assets, dir / "live"};
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

TEST_F(WorldViewPanelTest, ThreeDimensionalCameraSimultaneousDragDoesNotRollView) {
    const std::filesystem::path scene_3d = write_world("orbit.world", R"(
version = 1
active_camera = "cam"

[[entities]]
id = "cam"
transform = { position = [0.0, 0.0, 10.0] }
camera = { projection = "perspective", fov_y = 1.0 }
)");
    ASSERT_TRUE(panel.open_world(scene_3d));

    // Mouse down and drag diagonally: left (-50) and up (-40) simultaneously.
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Down,
            .position = {100.0f, 150.0f},
            .button = engine::MouseButton::Right,
    });
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Move,
            .position = {50.0f, 110.0f},
            .relative = {-50.0f, -40.0f},
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();

    EXPECT_GT(panel.camera_euler_xyz().x, 0.0f) << "Pitched up";
    EXPECT_GT(panel.camera_euler_xyz().y, 0.0f) << "Yawed left";

    // In Unity-style camera rotation, camera right axis stays strictly horizontal in world space (Y == 0).
    const glm::mat4 world_matrix = panel.camera_world_matrix();
    const glm::vec3 right{world_matrix[0]};
    const glm::vec3 up{world_matrix[1]};
    const glm::vec3 forward = panel.camera_rotation() * glm::vec3{0.0f, 0.0f, -1.0f};

    EXPECT_NEAR(right.y, 0.0f, 1e-5f) << "Camera right vector must have 0 Y (zero roll/tilt)";
    EXPECT_GT(up.y, 0.0f) << "Camera up vector must point upward";
    EXPECT_LT(forward.x, 0.0f) << "Looking left";
    EXPECT_GT(forward.y, 0.0f) << "Looking up";
}

TEST_F(WorldViewPanelTest, DraggingChangesCursorToMoveAndResetsOnRelease) {
    const std::filesystem::path scene = write_world("cursor.world", "version = 1\n");
    ASSERT_TRUE(panel.open_world(scene));

    // Right drag starts: cursor becomes Move.
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Down,
            .position = {100.0f, 150.0f},
            .button = engine::MouseButton::Right,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();
    EXPECT_EQ(engine::ui::presentation_of(editor_world).cursors.cursors[kEditorWindow], Cursor::Move);

    // Mouse move: cursor stays Move.
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Move,
            .position = {110.0f, 160.0f},
            .relative = {10.0f, 10.0f},
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();
    EXPECT_EQ(engine::ui::presentation_of(editor_world).cursors.cursors[kEditorWindow], Cursor::Move);

    // Mouse up: cursor resets to Default.
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Up,
            .position = {110.0f, 160.0f},
            .button = engine::MouseButton::Right,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();
    EXPECT_EQ(engine::ui::presentation_of(editor_world).cursors.cursors[kEditorWindow], Cursor::Default);
}

TEST_F(WorldViewPanelTest, GizmoHiddenWithoutEntitySelectionAndVisibleWithTransform) {
    const std::filesystem::path scene = write_world("gizmo_vis.world", R"(
version = 1

[[entities]]
id = "player"
transform = { position = [0.0, 0.0, 0.0] }
)");
    ASSERT_TRUE(panel.open_world(scene));
    ASSERT_EQ(panel.edit_world().alive_entities().size(), 1u);
    const engine::ecs::Entity ent = panel.edit_world().alive_entities()[0];

    // Frame without selection -> Gizmo is not visible.
    panel.frame(editor_world, canvas, true);
    EXPECT_FALSE(panel.gizmo_visible());

    // Select entity with Transform -> Gizmo becomes visible.
    selection.select(editor::EntitySelection{.entity = ent});
    panel.frame(editor_world, canvas, true);
    EXPECT_TRUE(panel.gizmo_visible());
    EXPECT_EQ(panel.gizmo_world_position(), (glm::vec3{0.0f, 0.0f, 0.0f}));
    EXPECT_GT(panel.gizmo_scale(), 0.0f);
    EXPECT_FALSE(panel.gizmo_has_z());

    // Clear selection -> Gizmo is hidden again.
    selection.clear();
    panel.frame(editor_world, canvas, true);
    EXPECT_FALSE(panel.gizmo_visible());
}

TEST_F(WorldViewPanelTest, GizmoDraggingXAxisTranslatesEntityInTwoDimensions) {
    const std::filesystem::path scene = write_world("gizmo_drag_2d.world", R"(
version = 1

[[entities]]
id = "player"
transform = { position = [0.0, 0.0, 0.0] }
)");
    ASSERT_TRUE(panel.open_world(scene));
    ASSERT_EQ(panel.edit_world().alive_entities().size(), 1u);
    const engine::ecs::Entity ent = panel.edit_world().alive_entities()[0];

    selection.select(editor::EntitySelection{.entity = ent});

    // Viewport is kCanvasRect with 36px header: left=40, top=116, width=400, height=200.
    // Entity is at (0, 0, 0), so gizmo origin is at local (200, 100) -> window (240, 216).
    // X axis extends to local (265, 100) -> window (305, 216).
    // Hover at local (230, 100) -> window (270, 216):
    engine::ui::pointer_for(editor_world, kEditorWindow).position = {270.0f, 216.0f};
    panel.frame(editor_world, canvas, true);
    EXPECT_EQ(panel.hovered_gizmo_axis(), editor::GizmoAxis::X);

    // Left mouse down at (270, 216) starts dragging X axis:
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Down,
            .position = {270.0f, 216.0f},
            .button = engine::MouseButton::Left,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();
    EXPECT_EQ(panel.active_gizmo_axis(), editor::GizmoAxis::X);

    // Drag 40px to the right: window position (310, 216)
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Move,
            .position = {310.0f, 216.0f},
            .relative = {40.0f, 0.0f},
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();

    const auto* tr = panel.edit_world().try_get<engine::Transform>(ent);
    ASSERT_NE(tr, nullptr);
    EXPECT_GT(tr->position.x, 0.0f);
    EXPECT_NEAR(tr->position.y, 0.0f, 1e-5f);
    EXPECT_NEAR(tr->position.z, 0.0f, 1e-5f);

    // Release mouse:
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Up,
            .position = {310.0f, 216.0f},
            .button = engine::MouseButton::Left,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();
    EXPECT_EQ(panel.active_gizmo_axis(), editor::GizmoAxis::None);
}

TEST_F(WorldViewPanelTest, GizmoDraggingInThreeDimensionalPerspective) {
    const std::filesystem::path scene_3d = write_world("gizmo_drag_3d.world", R"(
version = 1
active_camera = "cam"

[[entities]]
id = "cam"
transform = { position = [3.0, 4.0, 10.0] }
camera = { projection = "perspective", fov_y = 1.0 }

[[entities]]
id = "target"
transform = { position = [0.0, 0.0, 0.0] }
)");
    ASSERT_TRUE(panel.open_world(scene_3d));
    const auto entities = panel.edit_world().alive_entities();
    engine::ecs::Entity target_ent{};
    for (const auto e : entities) {
        if (panel.edit_world().try_get<engine::Camera>(e) == nullptr) {
            target_ent = e;
            break;
        }
    }
    ASSERT_TRUE(panel.edit_world().valid(target_ent));

    selection.select(editor::EntitySelection{.entity = target_ent});

    panel.frame(editor_world, canvas, true);
    EXPECT_TRUE(panel.gizmo_visible());
    EXPECT_TRUE(panel.gizmo_has_z());

    // Hover along X axis handle using exact projected midpoint
    const glm::vec3 x_mid_world = panel.gizmo_world_position() + glm::vec3{panel.gizmo_scale() * 0.5f, 0.0f, 0.0f};
    const engine::ui::WindowSize vp_size{
            static_cast<int>(std::round(kViewportRect.w)),
            static_cast<int>(std::round(kViewportRect.h)),
    };
    const glm::vec2 x_mid_screen =
            engine::world_to_screen(x_mid_world, panel.camera(), panel.camera_world_matrix(), vp_size);
    const glm::vec2 hover_window = glm::vec2{kViewportRect.x, kViewportRect.y} + x_mid_screen;

    engine::ui::pointer_for(editor_world, kEditorWindow).position = hover_window;
    panel.frame(editor_world, canvas, true);
    EXPECT_EQ(panel.hovered_gizmo_axis(), editor::GizmoAxis::X);

    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Down,
            .position = hover_window,
            .button = engine::MouseButton::Left,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();
    EXPECT_EQ(panel.active_gizmo_axis(), editor::GizmoAxis::X);

    // Drag along X
    const glm::vec2 drag_window = hover_window + glm::vec2{40.0f, 0.0f};
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Move,
            .position = drag_window,
            .relative = {40.0f, 0.0f},
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();

    const auto* tr = panel.edit_world().try_get<engine::Transform>(target_ent);
    ASSERT_NE(tr, nullptr);
    EXPECT_GT(tr->position.x, 0.0f);
    EXPECT_NEAR(tr->position.y, 0.0f, 1e-4f);
    EXPECT_NEAR(tr->position.z, 0.0f, 1e-4f);
}

class StubMesh final : public engine::render::IMesh {};
class StubGraphicFactory final : public engine::render::IGraphicFactory {
public:
    std::shared_ptr<engine::render::IMesh> create_mesh(const engine::render::MeshDesc&) override {
        return std::make_shared<StubMesh>();
    }
    std::shared_ptr<engine::render::IShader> create_shader(const engine::render::ShaderDesc&) override {
        return {};
    }
    std::shared_ptr<engine::render::ITexture> create_texture(const engine::render::TextureDesc&) override {
        return {};
    }
};

class QuietFatal final : public engine::IFatalError {
public:
    [[noreturn]] void report(std::string_view) override {
        throw std::runtime_error("unexpected fatal");
    }
};

TEST_F(WorldViewPanelTest, WorldSpaceGizmoAndOutlineRenderingPushesDrawMeshCommands) {
    const std::filesystem::path scene = write_world("gizmo_render.world", R"(
version = 1

[[entities]]
id = "box"
transform = { position = [1.0, 2.0, 0.0] }
sprite = { pixel_size = [100.0, 100.0], pixels_per_unit = 100.0 }
)");
    ASSERT_TRUE(panel.open_world(scene));
    ASSERT_EQ(panel.edit_world().alive_entities().size(), 1u);
    const engine::ecs::Entity ent = panel.edit_world().alive_entities()[0];

    selection.select(editor::EntitySelection{.entity = ent});
    panel.frame(editor_world, canvas, true);
    EXPECT_TRUE(panel.gizmo_visible());

    QuietFatal fatal;
    engine::AssetsDb assets(fatal);
    StubGraphicFactory factory;
    assets.set_graphic_factory(&factory);
    panel.set_assets(&assets);

    engine::render::CommandBuffer window_commands;
    const engine::EngineSystemDeps deps{
            .assets = &assets,
            .commands_for_window = [&](engine::WindowId id) -> engine::render::CommandBuffer* {
                return id == kEditorWindow ? &window_commands : nullptr;
            },
    };

    panel.render(editor_world, canvas, deps);

    // First command is CmdSetView from render_world_viewport.
    ASSERT_GE(window_commands.size(), 2u);
    EXPECT_NE(std::get_if<engine::render::CmdSetView>(&window_commands[0]), nullptr);

    // Followed by CmdDrawMesh for outline (4 edges for sprite) and gizmo (center + Y shaft/cone + X shaft/cone = 5).
    std::size_t mesh_draw_count = 0;
    for (std::size_t i = 1; i < window_commands.size(); ++i) {
        if (std::get_if<engine::render::CmdDrawMesh>(&window_commands[i]) != nullptr) {
            ++mesh_draw_count;
        }
    }
    EXPECT_EQ(mesh_draw_count, 9u);

    // Switch to Scale mode and verify 9 meshes (4 outline + 1 center cube + 2 Y shaft/cube + 2 X shaft/cube)
    panel.set_tool_scale();
    window_commands.clear();
    panel.render(editor_world, canvas, deps);
    mesh_draw_count = 0;
    for (std::size_t i = 1; i < window_commands.size(); ++i) {
        if (std::get_if<engine::render::CmdDrawMesh>(&window_commands[i]) != nullptr) {
            ++mesh_draw_count;
        }
    }
    EXPECT_EQ(mesh_draw_count, 9u);

    // Switch to Rotate mode and verify 5 meshes (4 outline + 1 Z torus ring in 2D)
    panel.set_tool_rotate();
    window_commands.clear();
    panel.render(editor_world, canvas, deps);
    mesh_draw_count = 0;
    for (std::size_t i = 1; i < window_commands.size(); ++i) {
        if (std::get_if<engine::render::CmdDrawMesh>(&window_commands[i]) != nullptr) {
            ++mesh_draw_count;
        }
    }
    EXPECT_EQ(mesh_draw_count, 5u);
}

TEST_F(WorldViewPanelTest, ToolSwitchingViaCommandsAndHotkeys) {
    const editor::WorldViewViewModel& vm = *panel.view_model();
    EXPECT_EQ(panel.gizmo_mode(), editor::GizmoMode::Translate);
    EXPECT_EQ(vm.moveBtnLabel.get(), "[ Move ]");
    EXPECT_EQ(vm.rotateBtnLabel.get(), "Rotate");
    EXPECT_EQ(vm.scaleBtnLabel.get(), "Scale");

    panel.view_model()->setToolRotate.execute();
    EXPECT_EQ(panel.gizmo_mode(), editor::GizmoMode::Rotate);
    EXPECT_EQ(vm.moveBtnLabel.get(), "Move");
    EXPECT_EQ(vm.rotateBtnLabel.get(), "[ Rotate ]");
    EXPECT_EQ(vm.scaleBtnLabel.get(), "Scale");

    panel.view_model()->setToolScale.execute();
    EXPECT_EQ(panel.gizmo_mode(), editor::GizmoMode::Scale);
    EXPECT_EQ(vm.moveBtnLabel.get(), "Move");
    EXPECT_EQ(vm.rotateBtnLabel.get(), "Rotate");
    EXPECT_EQ(vm.scaleBtnLabel.get(), "[ Scale ]");

    const std::filesystem::path scene = write_world("hotkeys.world", R"(
version = 1
[[entities]]
id = "box"
transform = { position = [0.0, 0.0, 0.0] }
)");
    ASSERT_TRUE(panel.open_world(scene));

    // Hotkey W -> Translate
    engine::ecs::EventWriter<engine::KeyEvent>{editor_world}.send(engine::KeyEvent{
            .window = kEditorWindow,
            .key = engine::KeyCode::W,
            .down = true,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();
    EXPECT_EQ(panel.gizmo_mode(), editor::GizmoMode::Translate);
    EXPECT_EQ(vm.moveBtnLabel.get(), "[ Move ]");

    // Hotkey E -> Rotate
    engine::ecs::EventWriter<engine::KeyEvent>{editor_world}.send(engine::KeyEvent{
            .window = kEditorWindow,
            .key = engine::KeyCode::E,
            .down = true,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();
    EXPECT_EQ(panel.gizmo_mode(), editor::GizmoMode::Rotate);
    EXPECT_EQ(vm.rotateBtnLabel.get(), "[ Rotate ]");

    // Hotkey R -> Scale
    engine::ecs::EventWriter<engine::KeyEvent>{editor_world}.send(engine::KeyEvent{
            .window = kEditorWindow,
            .key = engine::KeyCode::R,
            .down = true,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();
    EXPECT_EQ(panel.gizmo_mode(), editor::GizmoMode::Scale);
    EXPECT_EQ(vm.scaleBtnLabel.get(), "[ Scale ]");
}

TEST_F(WorldViewPanelTest, ScaleGizmoDraggingModifiesTransformScale) {
    const std::filesystem::path scene = write_world("scale_drag.world", R"(
version = 1
[[entities]]
id = "box"
transform = { position = [0.0, 0.0, 0.0], scale = [1.0, 1.0, 1.0] }
)");
    ASSERT_TRUE(panel.open_world(scene));
    const engine::ecs::Entity ent = panel.edit_world().alive_entities()[0];
    selection.select(editor::EntitySelection{.entity = ent});

    panel.set_tool_scale();
    panel.frame(editor_world, canvas, true);
    EXPECT_TRUE(panel.gizmo_visible());

    // Hover along X axis at window (270, 216)
    engine::ui::pointer_for(editor_world, kEditorWindow).position = {270.0f, 216.0f};
    panel.frame(editor_world, canvas, true);
    EXPECT_EQ(panel.hovered_gizmo_axis(), editor::GizmoAxis::X);

    // Left mouse down at (270, 216)
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Down,
            .position = {270.0f, 216.0f},
            .button = engine::MouseButton::Left,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();
    EXPECT_EQ(panel.active_gizmo_axis(), editor::GizmoAxis::X);

    // Drag 40px right to (310, 216)
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Move,
            .position = {310.0f, 216.0f},
            .relative = {40.0f, 0.0f},
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();

    const auto* tr = panel.edit_world().try_get<engine::Transform>(ent);
    ASSERT_NE(tr, nullptr);
    EXPECT_GT(tr->scale.x, 1.0f);
    EXPECT_FLOAT_EQ(tr->scale.y, 1.0f);
    EXPECT_FLOAT_EQ(tr->scale.z, 1.0f);

    // Release mouse
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Up,
            .position = {310.0f, 216.0f},
            .button = engine::MouseButton::Left,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();

    // Now test center uniform scaling
    engine::ui::pointer_for(editor_world, kEditorWindow).position = {240.0f, 216.0f};
    panel.frame(editor_world, canvas, true);
    EXPECT_EQ(panel.hovered_gizmo_axis(), editor::GizmoAxis::Center);

    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Down,
            .position = {240.0f, 216.0f},
            .button = engine::MouseButton::Left,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();
    EXPECT_EQ(panel.active_gizmo_axis(), editor::GizmoAxis::Center);

    const glm::vec3 pre_center_scale = tr->scale;
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Move,
            .position = {270.0f, 186.0f},
            .relative = {30.0f, -30.0f},
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();

    EXPECT_GT(tr->scale.x, pre_center_scale.x);
    EXPECT_GT(tr->scale.y, pre_center_scale.y);
    EXPECT_GT(tr->scale.z, pre_center_scale.z);
}

TEST_F(WorldViewPanelTest, RotateGizmoDraggingModifiesTransformRotation) {
    const std::filesystem::path scene = write_world("rotate_drag.world", R"(
version = 1
[[entities]]
id = "box"
transform = { position = [0.0, 0.0, 0.0] }
)");
    ASSERT_TRUE(panel.open_world(scene));
    const engine::ecs::Entity ent = panel.edit_world().alive_entities()[0];
    selection.select(editor::EntitySelection{.entity = ent});

    panel.set_tool_rotate();
    panel.frame(editor_world, canvas, true);
    EXPECT_TRUE(panel.gizmo_visible());

    // In 2D, only Z ring is active. Hover at radius (240 + 65, 216) = (305, 216)
    engine::ui::pointer_for(editor_world, kEditorWindow).position = {305.0f, 216.0f};
    panel.frame(editor_world, canvas, true);
    EXPECT_EQ(panel.hovered_gizmo_axis(), editor::GizmoAxis::Z);

    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Down,
            .position = {305.0f, 216.0f},
            .button = engine::MouseButton::Left,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();
    EXPECT_EQ(panel.active_gizmo_axis(), editor::GizmoAxis::Z);

    // Drag upwards to (305, 151) -> angle changes towards +Y
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Move,
            .position = {240.0f, 151.0f},
            .relative = {-65.0f, -65.0f},
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();

    const auto* tr = panel.edit_world().try_get<engine::Transform>(ent);
    ASSERT_NE(tr, nullptr);
    EXPECT_GT(std::abs(tr->rotation.z), 0.1f);
}

TEST_F(WorldViewPanelTest, GizmoDragMarksDirtyAndSaveWorldWritesToDiskAndClearsDirty) {
    const std::filesystem::path scene = write_world("save_test.world", R"(
version = 1
[[entities]]
id = "hero"
transform = { position = [0.0, 0.0, 0.0] }
)");
    ASSERT_TRUE(panel.open_world(scene));
    EXPECT_FALSE(panel.is_dirty());

    const engine::ecs::Entity ent = panel.edit_world().alive_entities()[0];
    selection.select(editor::EntitySelection{.entity = ent});

    panel.frame(editor_world, canvas, true);
    // Hover along X axis at (270, 216)
    engine::ui::pointer_for(editor_world, kEditorWindow).position = {270.0f, 216.0f};
    panel.frame(editor_world, canvas, true);
    EXPECT_EQ(panel.hovered_gizmo_axis(), editor::GizmoAxis::X);

    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Down,
            .position = {270.0f, 216.0f},
            .button = engine::MouseButton::Left,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();
    EXPECT_EQ(panel.active_gizmo_axis(), editor::GizmoAxis::X);

    // Drag to (310, 216)
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Move,
            .position = {310.0f, 216.0f},
            .relative = {40.0f, 0.0f},
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();

    EXPECT_TRUE(panel.is_dirty());

    // Release mouse
    engine::ecs::EventWriter<engine::MouseEvent>{editor_world}.send(engine::MouseEvent{
            .window = kEditorWindow,
            .kind = engine::MouseEvent::Kind::Up,
            .position = {280.0f, 216.0f},
            .button = engine::MouseButton::Left,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();

    EXPECT_TRUE(panel.is_dirty());

    // Saving clears dirty flag and updates file on disk
    EXPECT_TRUE(panel.save_world());
    EXPECT_FALSE(panel.is_dirty());

    std::ifstream in(scene);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("hero"), std::string::npos);
    EXPECT_NE(content.find("position"), std::string::npos);
}

TEST_F(WorldViewPanelTest, CtrlSHotkeyTriggersSaveWorld) {
    const std::filesystem::path scene = write_world("ctrl_s_test.world", R"(
version = 1
[[entities]]
id = "hero"
transform = { position = [0.0, 0.0, 0.0] }
)");
    ASSERT_TRUE(panel.open_world(scene));
    panel.mark_dirty();
    EXPECT_TRUE(panel.is_dirty());

    // LCtrl down
    engine::ecs::EventWriter<engine::KeyEvent>{editor_world}.send(engine::KeyEvent{
            .window = kEditorWindow,
            .key = engine::KeyCode::LCtrl,
            .down = true,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();

    // S down while Ctrl held
    engine::ecs::EventWriter<engine::KeyEvent>{editor_world}.send(engine::KeyEvent{
            .window = kEditorWindow,
            .key = engine::KeyCode::S,
            .down = true,
    });
    panel.frame(editor_world, canvas, true);
    editor_world.flush_events();

    EXPECT_FALSE(panel.is_dirty());
}

}


