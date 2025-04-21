#include <gtest/gtest.h>

#include "profiler_panel.h"

#include <engine/ecs/schedule.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/profiler.h>
#include <engine/ui/view_model.h>

#include <format>
#include <memory>
#include <string>
#include <string_view>

namespace {

class EmptyModel final : public engine::ui::ViewModel {};

engine::ecs::Entity spawn_named(engine::ecs::World& world, std::string_view id) {
    auto document = engine::ui::parse_xml(std::format(R"(<Canvas id="{}"><Label>Hi</Label></Canvas>)", id));
    EXPECT_TRUE(document.has_value());
    engine::ui::UiCanvas canvas;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.rect = {0.0f, 0.0f, 120.0f, 80.0f};
    canvas.data_context = std::make_shared<EmptyModel>();
    return engine::ui::spawn_canvas(world, canvas, std::move(*document));
}

}

TEST(ProfilerPanel, IdleShowsAHintAndNoData) {
    editor::ProfilerPanel panel;
    panel.refresh();
    const editor::ProfilerViewModel& vm = *panel.view_model();
    EXPECT_FALSE(panel.attached());
    EXPECT_TRUE(vm.canvases.get().empty());
    EXPECT_TRUE(vm.chart.columns().empty());
    EXPECT_TRUE(vm.shared.columns().empty());
    EXPECT_TRUE(vm.stats.get().empty());
    EXPECT_FALSE(vm.hint.get().empty());
}

#if defined(ENGINE_UI_PROFILER)

TEST(ProfilerPanel, CanvasesWithoutFrames) {
    engine::ecs::World game;
    (void) spawn_named(game, "hud");
    editor::ProfilerPanel panel;
    panel.attach(game);
    EXPECT_TRUE(engine::ui::ui_profiler_attached(game));
    panel.refresh();

    const editor::ProfilerViewModel& vm = *panel.view_model();
    ASSERT_EQ(vm.canvases.get().size(), 1u);
    EXPECT_EQ(vm.canvases.get()[0]->label.get(), "hud");
    EXPECT_EQ(vm.canvases.get()[0]->rowFill.get(), "#2f5d3a") << "the first canvas is selected";
    EXPECT_EQ(vm.stats.get(), "No frames yet");
    EXPECT_TRUE(vm.chart.columns().empty());
    EXPECT_TRUE(vm.hint.get().empty());
    panel.detach();
}

TEST(ProfilerPanel, FramesFillTheChartsAndTheNumbers) {
    engine::ecs::World game;
    const engine::ecs::Entity alpha = spawn_named(game, "alpha");
    const engine::ecs::Entity beta = spawn_named(game, "beta");
    engine::register_engine_systems(game);
    editor::ProfilerPanel panel;
    panel.attach(game);
    for (int i = 0; i < 3; ++i) {
        game.run(engine::ecs::Schedule::Frame);
    }
    engine::ui::begin_frame(game);
    panel.refresh();

    const editor::ProfilerViewModel& vm = *panel.view_model();
    ASSERT_EQ(vm.canvases.get().size(), 2u);
    EXPECT_EQ(engine::ui::profiler_selected(game), alpha);
    EXPECT_FALSE(vm.chart.columns().empty());
    EXPECT_FALSE(vm.shared.columns().empty());
    EXPECT_NE(vm.stats.get().find("bindings  last"), std::string::npos) << vm.stats.get();
    EXPECT_NE(vm.stats.get().find("begin frame"), std::string::npos);
    EXPECT_NE(vm.chartTitle.get().find("alpha"), std::string::npos) << vm.chartTitle.get();

    vm.canvases.get()[1]->select.execute();
    EXPECT_EQ(engine::ui::profiler_selected(game), beta);
    panel.refresh();
    EXPECT_EQ(vm.canvases.get()[1]->rowFill.get(), "#2f5d3a");
    EXPECT_NE(vm.chartTitle.get().find("beta"), std::string::npos);
    panel.detach();
}

TEST(ProfilerPanel, PauseIsTwoWayAndDetachClears) {
    engine::ecs::World game;
    (void) spawn_named(game, "hud");
    editor::ProfilerPanel panel;
    panel.attach(game);
    panel.refresh();
    editor::ProfilerViewModel& vm = *panel.view_model();

    vm.pause = true;  // the checkbox click writes the view-model
    panel.refresh();
    EXPECT_TRUE(engine::ui::profiler_paused(game));
    engine::ui::set_profiler_paused(game, false);
    panel.refresh();
    EXPECT_FALSE(vm.pause.get());

    const std::shared_ptr<editor::ProfilerRowViewModel> kept = vm.canvases.get()[0];
    panel.detach();
    EXPECT_FALSE(engine::ui::ui_profiler_attached(game));
    EXPECT_TRUE(vm.canvases.get().empty());
    EXPECT_TRUE(vm.chart.columns().empty());
    kept->select.execute();
    EXPECT_EQ(engine::ui::profiler_selected(game), engine::ecs::Entity{}) << "a stale row does nothing";
}

#else

TEST(ProfilerPanel, ReleaseBuildShowsWhyItIsEmpty) {
    engine::ecs::World game;
    (void) spawn_named(game, "hud");
    editor::ProfilerPanel panel;
    panel.attach(game);
    panel.refresh();
    const editor::ProfilerViewModel& vm = *panel.view_model();
    EXPECT_NE(vm.hint.get().find("not in this build"), std::string::npos);
    EXPECT_TRUE(vm.canvases.get().empty());
    panel.detach();
}

#endif
