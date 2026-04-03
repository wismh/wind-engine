#include "editor_panels.h"

#include "toolbar.h"

#include <asset_ids.h>

#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/presentation.h>

#include <algorithm>

namespace editor {
namespace {

// Above the editor's own canvas (order 0).
constexpr int kPanelOrder = 1;

engine::ecs::Entity spawn_panel(engine::ecs::World& world, engine::WindowId window, engine::AssetId document,
        std::shared_ptr<engine::ui::ViewModel> view_model) {
    const engine::ecs::Entity canvas = world.create();
    world.emplace<engine::ui::UiCanvas>(canvas, engine::ui::UiCanvas{
            .document = document,
            .stylesheet = assets::css::panels,
            .data_context = std::move(view_model),
            .fit = engine::ui::UiFit::Fixed,
            .order = kPanelOrder,
            .window = window,
    });
    return canvas;
}

void place(engine::ecs::World& world, engine::ecs::Entity canvas, const engine::render::Rect& rect) {
    if (engine::ui::UiCanvas* ui = world.try_get<engine::ui::UiCanvas>(canvas)) {
        ui->rect = rect;
    }
}

}

EditorPanels::EditorPanels(const Toolbar& toolbar) : toolbar_(&toolbar) {}

void EditorPanels::spawn(engine::ecs::World& world, engine::WindowId window) {
    window_ = window;
    inspector_canvas_ = spawn_panel(world, window, assets::ui::inspector, inspector_.view_model());
    profiler_canvas_ = spawn_panel(world, window, assets::ui::profiler, profiler_.view_model());
}

void EditorPanels::attach(engine::ecs::World& game) {
    inspector_.attach(game);
    profiler_.attach(game);
}

void EditorPanels::detach() {
    inspector_.detach();
    profiler_.detach();
}

void EditorPanels::frame(engine::ecs::World& world) {
    const engine::ui::WindowSize size = engine::ui::window_size_for(world, window_);
    const float width = static_cast<float>(size.width);
    const float height = std::max(0.0f, static_cast<float>(size.height) - kPanelTop);
    const engine::render::Rect shown{0.0f, kPanelTop, width, height};
    const engine::render::Rect hidden{};
    const bool inspector = toolbar_->active_tab() == EditorTab::Inspector;
    place(world, inspector_canvas_, inspector ? shown : hidden);
    place(world, profiler_canvas_, inspector ? hidden : shown);
    if (inspector) {
        inspector_.refresh();
    } else {
        profiler_.refresh();
    }
}

InspectorPanel& EditorPanels::inspector() {
    return inspector_;
}

ProfilerPanel& EditorPanels::profiler() {
    return profiler_;
}

}
