#include "editor_panels.h"

#include <asset_ids.h>

#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/dock_space.h>
#include <engine/ui/document.h>
#include <engine/ui/presentation.h>
#include <engine/ui/tree.h>

#include <algorithm>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

namespace editor {
namespace {

using engine::ui::DockLayout;
using engine::ui::DockNodeId;
using engine::ui::DockZone;

engine::ecs::Entity spawn_panel(engine::ecs::World& world, engine::WindowId window, engine::AssetId document,
        std::shared_ptr<engine::ui::ViewModel> view_model) {
    const engine::ecs::Entity canvas = world.create();
    world.emplace<engine::ui::UiCanvas>(canvas, engine::ui::UiCanvas{
            .document = document,
            .stylesheet = assets::css::panels,
            .data_context = std::move(view_model),
            .fit = engine::ui::UiFit::Fixed,
            .order = EditorPanels::kDockOrder + 1,
            .window = window,
    });
    return canvas;
}

std::vector<std::string> registered_keys() {
    std::vector<std::string> keys;
    for (const std::string_view key : EditorPanels::kKeys) {
        keys.emplace_back(key);
    }
    return keys;
}

// Drops panels this editor does not have and puts back the ones the layout lacks: tabbed with the Inspector, or the
// first stack when the Inspector is missing too.
bool reconcile(DockLayout& layout) {
    const std::vector<std::string> keys = registered_keys();
    return engine::ui::reconcile_dock_layout(layout, keys,
            engine::ui::DockSpot{.beside = std::string(EditorPanels::kInspector), .zone = DockZone::Center});
}

// The saved layout when it holds at least one of the editor's panels, else the default.
DockLayout initial_layout(const DockLayoutFile& file) {
    std::optional<DockLayout> saved = file.load();
    const auto known = [&](std::string_view key) { return saved->contains(key); };
    if (!saved || std::ranges::none_of(EditorPanels::kKeys, known)) {
        return EditorPanels::default_layout();
    }
    reconcile(*saved);
    return std::move(*saved);
}

}

EditorPanels::EditorPanels(DockLayoutFile layout_file) : layout_file_(std::move(layout_file)) {}

DockLayout EditorPanels::default_layout() {
    DockLayout layout;
    layout.add(std::string(kProject), {});
    const DockNodeId project = layout.find(kProject)->stack;
    layout.add(std::string(kInspector), {project, DockZone::Right});
    const DockNodeId inspector = layout.find(kInspector)->stack;
    layout.add(std::string(kBuild), {inspector, DockZone::Bottom});
    layout.add(std::string(kProfiler), {inspector, DockZone::Center});
    layout.activate(kInspector);
    // Project | right column: a quarter of the width. Inspector and Profiler over Build: Build gets 30%.
    layout.set_ratio(layout.root(), 0.25f);
    layout.set_ratio(layout.node(inspector)->parent, 0.7f);
    return layout;
}

void EditorPanels::spawn(engine::ecs::World& world, engine::WindowId window, engine::IWindowControl& windows) {
    world_ = &world;
    windows_ = &windows;
    window_ = window;
    explorer_canvas_ = spawn_panel(world, window, assets::ui::explorer, explorer_.view_model());
    inspector_canvas_ = spawn_panel(world, window, assets::ui::inspector, inspector_.view_model());
    profiler_canvas_ = spawn_panel(world, window, assets::ui::profiler, profiler_.view_model());
    build_canvas_ = spawn_panel(world, window, assets::ui::build, build_.view_model());

    engine::ui::DockSpace space;
    space.window = window;
    space.order = kDockOrder;
    // A floated panel gets an OS window of its own, bound to this world.
    space.float_mode = engine::ui::DockFloatMode::OsWindow;
    space.panels = {
            {std::string(kProject), "Project", explorer_canvas_},
            {std::string(kInspector), "Inspector", inspector_canvas_},
            {std::string(kProfiler), "Profiler", profiler_canvas_},
            {std::string(kBuild), "Build", build_canvas_},
    };
    space.layout = initial_layout(layout_file_);
    dock_ = world.create();
    saved_revision_ = space.revision;
    world.emplace<engine::ui::DockSpace>(dock_, std::move(space));
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
    engine::ui::DockSpace& space = world.get<engine::ui::DockSpace>(dock_);
    const engine::ui::WindowSize size = engine::ui::window_size_for(world, window_);
    space.area = engine::render::Rect{0.0f, kToolbarHeight, static_cast<float>(size.width),
            std::max(0.0f, static_cast<float>(size.height) - kToolbarHeight)};
    read_tree_keys(world);
    if (space.layout.is_visible(kInspector)) {
        inspector_.refresh();
    }
    if (space.layout.is_visible(kProfiler)) {
        profiler_.refresh();
    }
    if (space.revision != saved_revision_) {
        saved_revision_ = space.revision;
        (void) layout_file_.save(space.layout);
    }
}

void EditorPanels::show(std::string_view key) {
    DockLayout& layout = layout_mut();
    if (!layout.contains(key)) {
        reconcile(layout);
    }
    const std::optional<engine::ui::DockPanelPlace> place = layout.find(key);
    if (!place) {
        return;
    }
    layout.activate(key);
    if (place->float_id != engine::ui::kNoDockNode) {
        layout.raise_float(place->float_id);
    }
    // A float in an OS window can be behind the editor or another application; raise_float orders virtual floats.
    if (const std::optional<engine::WindowId> window = engine::ui::dock_panel_os_window(*world_, dock_, key)) {
        windows_->raise(*window);
    }
}

void EditorPanels::save_layout() {
    (void) layout_file_.save(layout());
}

const DockLayout& EditorPanels::layout() const {
    return world_->get<engine::ui::DockSpace>(dock_).layout;
}

DockLayout& EditorPanels::layout_mut() {
    return world_->get<engine::ui::DockSpace>(dock_).layout;
}

engine::ecs::Entity EditorPanels::dock() const {
    return dock_;
}

engine::ecs::Entity EditorPanels::canvas(std::string_view key) const {
    if (key == kProject) {
        return explorer_canvas_;
    }
    if (key == kInspector) {
        return inspector_canvas_;
    }
    if (key == kProfiler) {
        return profiler_canvas_;
    }
    if (key == kBuild) {
        return build_canvas_;
    }
    return {};
}

engine::ecs::Entity EditorPanels::tree_canvas_under_pointer(engine::ecs::World& world, engine::WindowId window) const {
    const glm::vec2 pointer = engine::ui::pointer_for(world, window).position;
    engine::ecs::Entity top{};
    std::optional<int> top_order;
    auto view = world.view<engine::ui::UiCanvas>();
    for (const engine::ecs::Entity entity : view) {
        const engine::ui::UiCanvas& canvas = view.get<engine::ui::UiCanvas>(entity);
        if (canvas.window != window || !engine::ui::rect_contains(canvas.rect, pointer.x, pointer.y)) {
            continue;
        }
        if (!top_order || canvas.order > *top_order) {
            top = entity;
            top_order = canvas.order;
        }
    }
    return top == explorer_canvas_ || top == inspector_canvas_ ? top : engine::ecs::Entity{};
}

void EditorPanels::read_tree_keys(engine::ecs::World& world) {
    // The tree under the pointer of the window the key went to: the editor window, or a panel's own float window.
    engine::ecs::Entity tree{};
    std::optional<std::size_t> keep_in_view;
    for (const engine::KeyEvent& event : engine::ecs::EventReader<engine::KeyEvent>{world, key_cursor_}) {
        if (!event.down) {
            continue;
        }
        const std::optional<engine::ui::TreeNav> nav = engine::ui::tree_nav_for_key(event.key);
        const engine::ecs::Entity under = nav ? tree_canvas_under_pointer(world, event.window) : engine::ecs::Entity{};
        if (under == engine::ecs::Entity{}) {
            continue;
        }
        tree = under;
        const std::optional<std::size_t> row =
                tree == explorer_canvas_ ? explorer_.navigate(*nav) : inspector_.navigate(*nav);
        if (row) {
            keep_in_view = row;
        }
    }
    if (!keep_in_view) {
        return;
    }
    engine::ui::UiInstance* instance = world.try_get<engine::ui::UiInstance>(tree);
    if (instance == nullptr) {
        return;
    }
    engine::ui::Element* scroller = engine::ui::find_by_id(instance->document.root, "tree");
    const engine::ui::Element* rows = engine::ui::find_by_id(instance->document.root, "tree-rows");
    if (scroller != nullptr && rows != nullptr) {
        (void) engine::ui::scroll_item_into_view(*scroller, *rows, *keep_in_view);
    }
}

ExplorerPanel& EditorPanels::explorer() {
    return explorer_;
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
