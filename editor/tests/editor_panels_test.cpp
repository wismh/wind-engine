#include <gtest/gtest.h>

#include "editor_panels.h"

#include "fixtures/fake_services.h"

#include <engine/core/input_system.h>
#include <engine/core/worlds.h>
#include <engine/ecs/events.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/world.h>
#include <engine/render/command_buffer.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/dock_geometry.h>
#include <engine/ui/dock_space.h>
#include <engine/ui/inspector.h>
#include <engine/ui/presentation.h>
#include <engine/ui/profiler.h>
#include <engine/ui/view_model.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using engine::render::Rect;
using engine::ui::DockLayout;
using engine::ui::DockZone;

constexpr engine::WindowId kEditorWindow{1};
constexpr Rect kArea{0.0f, editor::EditorPanels::kToolbarHeight, 1280.0f, 800.0f - 56.0f};

class EmptyModel final : public engine::ui::ViewModel {};

// A game world with one canvas, for the Inspector and Profiler to read.
struct Game {
    Game() {
        engine::ui::presentation_of(world).sizes.sizes[engine::kPrimaryWindow] = {800, 600};
        auto document = engine::ui::parse_xml(R"(<Canvas id="hud"><Label>Hi</Label></Canvas>)");
        EXPECT_TRUE(document.has_value());
        engine::ui::UiCanvas canvas;
        canvas.fit = engine::ui::UiFit::Fixed;
        canvas.rect = {0.0f, 0.0f, 120.0f, 80.0f};
        canvas.data_context = std::make_shared<EmptyModel>();
        (void) engine::ui::spawn_canvas(world, canvas, std::move(*document));
    }

    engine::ecs::World world;
};

// The editor world with the engine systems and the panels' frame system, and a layout file per test.
class EditorPanelsTest : public ::testing::Test {
protected:
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "wind_editor_panels_test" /
            ::testing::UnitTest::GetInstance()->current_test_info()->name();
    fakes::FakeWindowControl windows;
    engine::render::CommandBuffer commands;
    engine::ecs::World world;
    std::optional<editor::EditorPanels> panels;

    void SetUp() override {
        std::filesystem::remove_all(dir);
        engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
        engine::ui::presentation_of(world).sizes.sizes[kEditorWindow] = {1280, 800};
    }

    void TearDown() override {
        panels.reset();
        std::error_code error;
        std::filesystem::remove_all(dir, error);
    }

    [[nodiscard]] std::filesystem::path layout_path() const { return dir / "dock_layout.toml"; }

    void start() {
        panels.emplace(editor::DockLayoutFile{layout_path()});
        panels->spawn(world, kEditorWindow, windows);
        world.add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game,
                [this](engine::ecs::World& w) { panels->frame(w); });
        frame();
    }

    void frame() {
        engine::ui::reset_pointer_frame(engine::ui::presentation_of(world));
        world.run(engine::ecs::Schedule::Frame);
        world.flush_events();
    }

    engine::ui::DockSpace& space() { return world.get<engine::ui::DockSpace>(panels->dock()); }

    const engine::ui::UiCanvas& canvas(std::string_view key) {
        return world.get<engine::ui::UiCanvas>(panels->canvas(key));
    }

    void write_file(const std::string& text) const {
        std::filesystem::create_directories(dir);
        std::ofstream(layout_path(), std::ios::binary) << text;
    }
};

TEST_F(EditorPanelsTest, DefaultLayoutHasProjectLeftToolsOverBuildInTheMiddleInspectorRight) {
    const DockLayout layout = editor::EditorPanels::default_layout();
    ASSERT_TRUE(layout.valid());
    EXPECT_TRUE(layout.floats().empty());
    const engine::ui::DockNode* root = layout.node(layout.root());
    ASSERT_EQ(root->kind, engine::ui::DockNodeKind::Split);
    EXPECT_EQ(root->axis, engine::ui::DockAxis::Horizontal);
    EXPECT_FLOAT_EQ(root->ratio, 0.2f);
    EXPECT_EQ(root->first, layout.find("project")->stack);

    const engine::ui::DockNode* rest = layout.node(root->second);
    ASSERT_EQ(rest->kind, engine::ui::DockNodeKind::Split);
    EXPECT_EQ(rest->axis, engine::ui::DockAxis::Horizontal);
    EXPECT_FLOAT_EQ(rest->ratio, 0.6f);
    EXPECT_EQ(rest->second, layout.find("inspector")->stack);

    const engine::ui::DockNode* tools = layout.node(rest->first);
    ASSERT_EQ(tools->kind, engine::ui::DockNodeKind::Split);
    EXPECT_EQ(tools->axis, engine::ui::DockAxis::Vertical);
    EXPECT_FLOAT_EQ(tools->ratio, 0.7f);
    EXPECT_EQ(layout.node(tools->first)->panels, (std::vector<std::string>{"ui_tree", "profiler"}));
    EXPECT_EQ(tools->second, layout.find("build")->stack);

    EXPECT_TRUE(layout.is_visible("project"));
    EXPECT_TRUE(layout.is_visible("ui_tree"));
    EXPECT_TRUE(layout.is_visible("inspector"));
    EXPECT_FALSE(layout.is_visible("profiler"));
    EXPECT_TRUE(layout.is_visible("build"));
}

TEST_F(EditorPanelsTest, TheDockSpaceFillsTheWindowBelowTheToolbarAndPlacesThePanels) {
    start();
    EXPECT_EQ(space().window, kEditorWindow);
    EXPECT_EQ(space().area, kArea);
    EXPECT_EQ(space().layout, editor::EditorPanels::default_layout()) << "no saved layout";
    ASSERT_EQ(space().panels.size(), 5u);
    EXPECT_EQ(space().panels[0].title, "Project");
    EXPECT_EQ(space().panels[1].title, "UI Tree");
    EXPECT_EQ(space().panels[2].title, "Inspector");
    EXPECT_EQ(space().panels[4].title, "Build");

    const engine::ui::DockGeometry geometry =
            engine::ui::compute_dock_geometry(space().layout, kArea, engine::ui::DockMetrics{});
    for (const std::string_view key : editor::EditorPanels::kKeys) {
        EXPECT_EQ(canvas(key).fit, engine::ui::UiFit::Fixed) << key;
        EXPECT_EQ(canvas(key).window, kEditorWindow) << key;
        const Rect expected = space().layout.is_visible(key) ? geometry.panel(key)->content : Rect{};
        EXPECT_EQ(canvas(key).rect, expected) << key;
    }
    EXPECT_EQ(canvas("project").rect.x, 0.0f);
    EXPECT_GT(canvas("project").rect.y, kArea.y) << "under its tab strip";
    EXPECT_GT(canvas("ui_tree").rect.x, canvas("project").rect.w);
    EXPECT_GT(canvas("inspector").rect.x, canvas("ui_tree").rect.x + canvas("ui_tree").rect.w);
    EXPECT_GT(canvas("build").rect.y, canvas("ui_tree").rect.y + canvas("ui_tree").rect.h);

    // A resized window moves the dock area with it.
    engine::ui::presentation_of(world).sizes.sizes[kEditorWindow] = {1000, 700};
    frame();
    EXPECT_EQ(space().area, (Rect{0.0f, 56.0f, 1000.0f, 644.0f}));
    const Rect project = canvas("project").rect;
    EXPECT_EQ(project.y + project.h, 700.0f);
}

TEST_F(EditorPanelsTest, OnlyTheShownUiTreeOrProfilerRefreshes) {
    start();
    Game game;
    panels->attach(game.world);
    frame();
    EXPECT_FALSE(panels->ui_tree().view_model()->rows.get().empty()) << "UI Tree is shown";
    EXPECT_TRUE(panels->profiler().view_model()->canvases.get().empty()) << "Profiler is a hidden tab";

    space().layout.activate("profiler");
    frame();
    EXPECT_FALSE(panels->profiler().view_model()->canvases.get().empty());
    EXPECT_EQ(canvas("ui_tree").rect, Rect{});
    EXPECT_NE(canvas("profiler").rect, Rect{});
    panels->detach();
}

TEST_F(EditorPanelsTest, TheInspectorShowsTheNewestSelectionOfEitherPanel) {
    const std::filesystem::path project = dir / "project";
    std::filesystem::create_directories(project);
    std::ofstream(project / "wind_project.toml") << "name = \"x\"\n";
    start();
    panels->explorer().open(project);
    Game game;
    panels->attach(game.world);
    const editor::InspectorViewModel& inspector = *panels->inspector().view_model();

    panels->explorer().select("wind_project.toml");
    frame();
    EXPECT_EQ(inspector.title.get(), "wind_project.toml");

    // A pick click in the game while the UI Tree is a hidden tab.
    space().layout.activate("profiler");
    const std::vector<engine::ui::InspectorTreeRow> rows = engine::ui::inspector_tree(game.world);
    ASSERT_EQ(rows.size(), 2u);
    engine::ui::inspector_select(game.world, rows[1].window, rows[1].pick);
    frame();
    EXPECT_EQ(inspector.title.get(), "Label");
    EXPECT_EQ(inspector.subtitle.get(), "UI element");
    EXPECT_EQ(panels->explorer().selected(), "wind_project.toml") << "the Project tab keeps its highlight";

    panels->explorer().select("wind_project.toml");
    frame();
    EXPECT_EQ(inspector.title.get(), "wind_project.toml");

    // Stop with an element selected: the Inspector empties.
    engine::ui::inspector_select(game.world, rows[1].window, rows[1].pick);
    frame();
    panels->detach();
    frame();
    EXPECT_EQ(inspector.title.get(), "Nothing selected");
}

TEST_F(EditorPanelsTest, ShowBringsAHiddenOrLostPanelBack) {
    start();
    DockLayout& layout = space().layout;
    ASSERT_TRUE(layout.move("build", {layout.find("inspector")->stack, DockZone::Center}));
    layout.activate("inspector");
    frame();
    EXPECT_FALSE(layout.is_visible("build"));
    EXPECT_EQ(canvas("build").rect, Rect{});

    panels->show(editor::EditorPanels::kBuild);
    frame();
    EXPECT_TRUE(space().layout.is_visible("build"));
    EXPECT_NE(canvas("build").rect, Rect{});

    ASSERT_TRUE(space().layout.remove("build"));
    panels->show(editor::EditorPanels::kBuild);
    EXPECT_TRUE(space().layout.is_visible("build"));

    // In a float under another float: raised to the top.
    ASSERT_TRUE(space().layout.float_panel("build", Rect{100.0f, 100.0f, 300.0f, 200.0f}));
    ASSERT_TRUE(space().layout.float_panel("project", Rect{150.0f, 150.0f, 300.0f, 200.0f}));
    panels->show(editor::EditorPanels::kBuild);
    EXPECT_EQ(space().layout.floats().back().id, space().layout.find("build")->float_id);
}

TEST_F(EditorPanelsTest, ADockSpaceChangeIsSavedAndTheNextStartReadsIt) {
    start();
    EXPECT_FALSE(std::filesystem::exists(layout_path())) << "nothing changed yet";

    DockLayout& layout = space().layout;
    ASSERT_TRUE(layout.float_panel("profiler", Rect{200.0f, 150.0f, 320.0f, 240.0f}));
    ASSERT_TRUE(layout.set_ratio(layout.root(), 0.4f));
    ++space().revision;
    frame();
    const DockLayout saved = space().layout;
    const std::optional<DockLayout> on_disk = editor::DockLayoutFile{layout_path()}.load();
    ASSERT_TRUE(on_disk.has_value());
    EXPECT_EQ(*on_disk, saved);

    // The next editor start.
    engine::ecs::World next_world;
    editor::EditorPanels next{editor::DockLayoutFile{layout_path()}};
    next.spawn(next_world, kEditorWindow, windows);
    EXPECT_EQ(next.layout(), saved);
}

TEST_F(EditorPanelsTest, SaveLayoutWritesWithoutAChange) {
    start();
    panels->save_layout();
    const std::optional<DockLayout> on_disk = editor::DockLayoutFile{layout_path()}.load();
    ASSERT_TRUE(on_disk.has_value());
    EXPECT_EQ(*on_disk, editor::EditorPanels::default_layout());
}

TEST_F(EditorPanelsTest, ACorruptLayoutFileStartsFromTheDefault) {
    write_file("version = 1\nroot = [");
    start();
    EXPECT_EQ(space().layout, editor::EditorPanels::default_layout());
}

TEST_F(EditorPanelsTest, ASavedLayoutLosesUnknownPanelsAndGetsTheMissingOnes) {
    DockLayout saved;
    saved.add("project", {});
    saved.add("ghost", {saved.find("project")->stack, DockZone::Right});
    write_file(engine::ui::dock_layout_to_text(saved));
    start();
    const DockLayout& layout = space().layout;
    EXPECT_TRUE(layout.valid());
    EXPECT_FALSE(layout.contains("ghost"));
    for (const std::string_view key : editor::EditorPanels::kKeys) {
        EXPECT_TRUE(layout.contains(key)) << key;
    }
}

TEST_F(EditorPanelsTest, ALayoutWithNoneOfThePanelsStartsFromTheDefault) {
    DockLayout saved;
    saved.add("ghost", {});
    write_file(engine::ui::dock_layout_to_text(saved));
    start();
    EXPECT_EQ(space().layout, editor::EditorPanels::default_layout());
}

TEST_F(EditorPanelsTest, TreeKeysGoToTheProjectPanelUnderThePointer) {
    const std::filesystem::path project = dir / "project";
    std::filesystem::create_directories(project / "assets");
    std::ofstream(project / "wind_project.toml") << "";
    start();
    panels->explorer().open(project);

    const auto press_down = [&] {
        engine::ecs::EventWriter<engine::KeyEvent>{world}.send(
                engine::KeyEvent{.window = kEditorWindow, .key = engine::KeyCode::Down, .down = true});
        frame();
    };
    const Rect build = canvas("build").rect;
    engine::ui::pointer_for(world, kEditorWindow).position = {build.x + 10.0f, build.y + 10.0f};
    press_down();
    EXPECT_TRUE(panels->explorer().selected().empty()) << "the pointer is over the Build panel";

    const Rect rect = canvas("project").rect;
    engine::ui::pointer_for(world, kEditorWindow).position = {rect.x + 10.0f, rect.y + 10.0f};
    press_down();
    EXPECT_EQ(panels->explorer().selected(), "assets");

    // A float over the Project panel takes the keys there.
    ASSERT_TRUE(space().layout.float_panel("build", Rect{0.0f, 60.0f, 300.0f, 300.0f}));
    frame();
    press_down();
    EXPECT_EQ(panels->explorer().selected(), "assets");
}

TEST_F(EditorPanelsTest, AttachAndDetachBothProbes) {
    start();
    engine::ecs::World game;

    panels->attach(game);
    EXPECT_TRUE(panels->ui_tree().attached());
    EXPECT_TRUE(panels->inspector().attached());
    EXPECT_TRUE(panels->profiler().attached());
    EXPECT_TRUE(engine::ui::inspector_attached(game));
    EXPECT_EQ(engine::ui::ui_profiler_attached(game), engine::ui::kUiProfilerBuilt);

    panels->detach();
    EXPECT_FALSE(panels->ui_tree().attached());
    EXPECT_FALSE(panels->inspector().attached());
    EXPECT_FALSE(panels->profiler().attached());
    EXPECT_FALSE(engine::ui::inspector_attached(game));
    EXPECT_FALSE(engine::ui::ui_profiler_attached(game));
}

// The editor world in a process with window control (the fake): floated panels get OS windows.
class EditorPanelsWindowsTest : public ::testing::Test {
protected:
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "wind_editor_panels_test" /
            ::testing::UnitTest::GetInstance()->current_test_info()->name();
    fakes::QuietFatal fatal;
    fakes::FakeWindowControl windows;
    engine::render::CommandBuffer commands;
    engine::Worlds worlds{fatal};
    engine::ecs::World* world = nullptr;
    engine::WindowId editor_window{};
    std::optional<editor::EditorPanels> panels;

    void SetUp() override {
        std::filesystem::remove_all(dir);
        worlds.set_deps(engine::EngineSystemDeps{.commands = &commands, .windows = &windows, .worlds = &worlds});
        world = &worlds.add();
        editor_window = *windows.open_window(
                engine::WindowDesc{.title = "Wind Editor", .size = {1280, 800}, .position = glm::ivec2{40, 30}});
        worlds.bind_window(editor_window, *world);
        worlds.enable_ui(*world);
        worlds.presentation().sizes.sizes[editor_window] = {1280, 800};
    }

    void TearDown() override {
        panels.reset();
        std::error_code error;
        std::filesystem::remove_all(dir, error);
    }

    [[nodiscard]] std::filesystem::path layout_path() const { return dir / "dock_layout.toml"; }

    void start() {
        panels.emplace(editor::DockLayoutFile{layout_path()});
        panels->spawn(*world, editor_window, windows);
        world->add_system(engine::ecs::Schedule::Frame, engine::ecs::Phase::Game,
                [this](engine::ecs::World& w) { panels->frame(w); });
        frame();
    }

    void frame() {
        engine::ui::reset_pointer_frame(worlds.presentation());
        world->run(engine::ecs::Schedule::Frame);
        world->flush_events();
    }

    engine::ui::DockSpace& space() { return world->get<engine::ui::DockSpace>(panels->dock()); }

    const engine::ui::UiCanvas& canvas(std::string_view key) {
        return world->get<engine::ui::UiCanvas>(panels->canvas(key));
    }
};

TEST_F(EditorPanelsWindowsTest, AFloatedPanelGetsAnOsWindowBoundToTheEditorWorld) {
    start();
    EXPECT_EQ(space().float_mode, engine::ui::DockFloatMode::OsWindow);
    EXPECT_EQ(windows.open.size(), 2u) << "the default layout has no float";

    ASSERT_TRUE(space().layout.float_panel("build", Rect{600.0f, 300.0f, 400.0f, 250.0f}));
    frame();
    ASSERT_EQ(windows.open.size(), 3u);
    const engine::WindowId window = windows.open[2];
    EXPECT_EQ(worlds.world_for(window), world);
    EXPECT_EQ(windows.opened.at(window).title, "Build");
    EXPECT_EQ(windows.opened.at(window).position, (glm::ivec2{640, 330}));
    EXPECT_EQ(windows.opened.at(window).size, (glm::ivec2{400, 250}));
    EXPECT_EQ(canvas("build").window, window);
    EXPECT_EQ(canvas("build").rect, (Rect{0.0f, 24.0f, 400.0f, 226.0f}));
    EXPECT_EQ(canvas("project").window, editor_window);
}

TEST_F(EditorPanelsWindowsTest, AFloatWindowIsAToolWindowOfTheEditorWindow) {
    start();
    ASSERT_TRUE(space().layout.float_panel("build", Rect{600.0f, 300.0f, 400.0f, 250.0f}));
    frame();
    const engine::WindowId window = canvas("build").window;
    ASSERT_NE(window, editor_window);
    EXPECT_EQ(windows.opened.at(window).owner, editor_window);
    EXPECT_TRUE(windows.opened.at(window).style.utility);
}

TEST_F(EditorPanelsWindowsTest, ShowRaisesTheOsWindowOfAFloatedPanel) {
    start();
    ASSERT_TRUE(space().layout.float_panel("build", Rect{600.0f, 300.0f, 400.0f, 250.0f}));
    frame();
    const engine::WindowId window = canvas("build").window;
    ASSERT_NE(window, editor_window);

    panels->show(editor::EditorPanels::kBuild);
    EXPECT_EQ(windows.raised, std::vector<engine::WindowId>{window});

    // A docked panel has no window of its own to raise.
    panels->show(editor::EditorPanels::kProject);
    EXPECT_EQ(windows.raised, std::vector<engine::WindowId>{window});
}

TEST_F(EditorPanelsWindowsTest, ASavedFloatOpensItsWindowAgainAndAMoveIsSaved) {
    DockLayout saved = editor::EditorPanels::default_layout();
    ASSERT_TRUE(saved.float_panel("profiler", Rect{700.0f, 120.0f, 360.0f, 260.0f}));
    std::filesystem::create_directories(dir);
    std::ofstream(layout_path(), std::ios::binary) << engine::ui::dock_layout_to_text(saved);

    start();
    EXPECT_EQ(space().layout, saved);
    ASSERT_EQ(windows.open.size(), 3u);
    const engine::WindowId window = windows.open[2];
    EXPECT_EQ(windows.opened.at(window).title, "Profiler");
    EXPECT_EQ(windows.opened.at(window).position, (glm::ivec2{740, 150}));
    EXPECT_EQ(windows.opened.at(window).size, (glm::ivec2{360, 260}));
    EXPECT_EQ(canvas("profiler").window, window);

    // The user moves the window: the layout follows, and the next frame saves it.
    windows.positions[window] = {900, 200};
    frame();
    frame();
    const std::optional<DockLayout> on_disk = editor::DockLayoutFile{layout_path()}.load();
    ASSERT_TRUE(on_disk.has_value());
    ASSERT_EQ(on_disk->floats().size(), 1u);
    EXPECT_EQ(on_disk->floats()[0].rect, (Rect{860.0f, 170.0f, 360.0f, 260.0f}));
}

TEST_F(EditorPanelsWindowsTest, TreeKeysGoToTheProjectPanelInItsOwnWindow) {
    const std::filesystem::path project = dir / "project";
    std::filesystem::create_directories(project / "assets");
    std::ofstream(project / "wind_project.toml") << "";
    start();
    panels->explorer().open(project);
    ASSERT_TRUE(space().layout.float_panel("project", Rect{100.0f, 100.0f, 300.0f, 300.0f}));
    frame();
    const engine::WindowId window = canvas("project").window;
    ASSERT_NE(window, editor_window);

    const Rect rect = canvas("project").rect;
    engine::ui::pointer_for(*world, window).position = {rect.x + 10.0f, rect.y + 10.0f};
    engine::ecs::EventWriter<engine::KeyEvent>{*world}.send(
            engine::KeyEvent{.window = window, .key = engine::KeyCode::Down, .down = true});
    frame();
    EXPECT_EQ(panels->explorer().selected(), "assets");
}

}
