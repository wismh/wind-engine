#include "editor_panels.h"

#include "toolbar.h"

#include <asset_ids.h>

#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/presentation.h>
#include <engine/ui/tree.h>

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
    build_canvas_ = spawn_panel(world, window, assets::ui::build, build_.view_model());
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
    const EditorTab tab = toolbar_->active_tab();
    place(world, inspector_canvas_, tab == EditorTab::Inspector ? shown : hidden);
    place(world, profiler_canvas_, tab == EditorTab::Profiler ? shown : hidden);
    place(world, build_canvas_, tab == EditorTab::Build ? shown : hidden);
    read_tree_keys(world, shown, tab == EditorTab::Inspector);
    if (tab == EditorTab::Inspector) {
        inspector_.refresh();
    } else if (tab == EditorTab::Profiler) {
        profiler_.refresh();
    }
}

void EditorPanels::read_tree_keys(engine::ecs::World& world, const engine::render::Rect& panel,
        bool inspector_shown) {
    std::optional<std::size_t> keep_in_view;
    const glm::vec2 pointer = engine::ui::pointer_for(world, window_).position;
    const bool over = inspector_shown && engine::ui::rect_contains(panel, pointer.x, pointer.y);
    for (const engine::KeyEvent& event : engine::ecs::EventReader<engine::KeyEvent>{world, key_cursor_}) {
        if (!over || !event.down || event.window != window_) {
            continue;
        }
        if (const std::optional<engine::ui::TreeNav> nav = engine::ui::tree_nav_for_key(event.key)) {
            if (const std::optional<std::size_t> row = inspector_.navigate(*nav)) {
                keep_in_view = row;
            }
        }
    }
    if (!keep_in_view) {
        return;
    }
    engine::ui::UiInstance* instance = world.try_get<engine::ui::UiInstance>(inspector_canvas_);
    if (instance == nullptr) {
        return;
    }
    engine::ui::Element* scroller = engine::ui::find_by_id(instance->document.root, "tree");
    const engine::ui::Element* rows = engine::ui::find_by_id(instance->document.root, "tree-rows");
    if (scroller != nullptr && rows != nullptr) {
        (void) engine::ui::scroll_item_into_view(*scroller, *rows, *keep_in_view);
    }
}

InspectorPanel& EditorPanels::inspector() {
    return inspector_;
}

ProfilerPanel& EditorPanels::profiler() {
    return profiler_;
}

BuildPanel& EditorPanels::build() {
    return build_;
}

}
