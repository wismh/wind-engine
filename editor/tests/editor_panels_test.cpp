#include <gtest/gtest.h>

#include "editor_panels.h"
#include "toolbar.h"

#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/inspector.h>
#include <engine/ui/presentation.h>
#include <engine/ui/profiler.h>

#include <vector>

namespace {

constexpr engine::WindowId kEditorWindow{1};

// The editor world in these tests holds only the panel canvases, in spawn order: Project, Inspector, Profiler,
// Build.
std::vector<engine::ui::UiCanvas*> panel_canvases(engine::ecs::World& world) {
    std::vector<engine::ui::UiCanvas*> out;
    auto view = world.view<engine::ui::UiCanvas>();
    for (const engine::ecs::Entity entity : view) {
        out.push_back(&view.get<engine::ui::UiCanvas>(entity));
    }
    return out;
}

bool same_rect(const engine::render::Rect& a, const engine::render::Rect& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

}

TEST(EditorTabs, ProjectFirstThenTheTabCommandsSwitch) {
    editor::Toolbar toolbar;
    editor::EditorViewModel& vm = *toolbar.view_model();
    EXPECT_EQ(toolbar.active_tab(), editor::EditorTab::Explorer);
    EXPECT_TRUE(vm.explorerTab.get());
    EXPECT_FALSE(vm.inspectorTab.get());

    vm.showInspector.execute();
    EXPECT_EQ(toolbar.active_tab(), editor::EditorTab::Inspector);
    EXPECT_FALSE(vm.explorerTab.get());
    EXPECT_TRUE(vm.inspectorTab.get());

    ASSERT_TRUE(vm.showProfiler.can_execute());
    vm.showProfiler.execute();
    EXPECT_EQ(toolbar.active_tab(), editor::EditorTab::Profiler);
    EXPECT_FALSE(vm.inspectorTab.get());
    EXPECT_TRUE(vm.profilerTab.get());

    vm.showBuild.execute();
    EXPECT_EQ(toolbar.active_tab(), editor::EditorTab::Build);
    EXPECT_FALSE(vm.profilerTab.get());
    EXPECT_TRUE(vm.buildTab.get());

    vm.showExplorer.execute();
    EXPECT_EQ(toolbar.active_tab(), editor::EditorTab::Explorer);
    EXPECT_TRUE(vm.explorerTab.get());
    EXPECT_FALSE(vm.buildTab.get());
    EXPECT_EQ(toolbar.take_request(), editor::EditorRequest::None) << "a tab is not a Play/Stop request";
}

TEST(EditorPanels, OnlyTheActiveTabHasARect) {
    editor::Toolbar toolbar;
    editor::EditorPanels panels{toolbar};
    engine::ecs::World world;
    engine::ui::presentation_of(world).sizes.sizes[kEditorWindow] = {1280, 800};
    panels.spawn(world, kEditorWindow);
    panels.frame(world);

    const std::vector<engine::ui::UiCanvas*> canvases = panel_canvases(world);
    ASSERT_EQ(canvases.size(), 4u);
    const engine::render::Rect shown{0.0f, editor::EditorPanels::kPanelTop, 1280.0f, 800.0f - 88.0f};
    for (const engine::ui::UiCanvas* canvas : canvases) {
        EXPECT_EQ(canvas->fit, engine::ui::UiFit::Fixed);
        EXPECT_EQ(canvas->window, kEditorWindow);
    }
    const auto only_shown = [&](std::size_t active) {
        for (std::size_t i = 0; i < canvases.size(); ++i) {
            EXPECT_TRUE(same_rect(canvases[i]->rect, i == active ? shown : engine::render::Rect{})) << "canvas " << i;
        }
    };
    only_shown(0);

    toolbar.view_model()->showInspector.execute();
    panels.frame(world);
    only_shown(1);

    toolbar.view_model()->showProfiler.execute();
    panels.frame(world);
    only_shown(2);

    toolbar.view_model()->showBuild.execute();
    panels.frame(world);
    only_shown(3);
}

TEST(EditorPanels, AttachAndDetachBothProbes) {
    editor::Toolbar toolbar;
    editor::EditorPanels panels{toolbar};
    engine::ecs::World game;

    panels.attach(game);
    EXPECT_TRUE(panels.inspector().attached());
    EXPECT_TRUE(panels.profiler().attached());
    EXPECT_TRUE(engine::ui::inspector_attached(game));
    EXPECT_EQ(engine::ui::ui_profiler_attached(game), engine::ui::kUiProfilerBuilt);

    panels.detach();
    EXPECT_FALSE(panels.inspector().attached());
    EXPECT_FALSE(panels.profiler().attached());
    EXPECT_FALSE(engine::ui::inspector_attached(game));
    EXPECT_FALSE(engine::ui::ui_profiler_attached(game));
}
