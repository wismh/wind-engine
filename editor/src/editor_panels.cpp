#include "editor_panels.h"

#include "canvas_pointer.h"
#include <asset_ids.h>

#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/dock_space.h>
#include <engine/ui/document.h>
#include <engine/ui/presentation.h>
#include <engine/ui/tree.h>

#include <algorithm>
#include <expected>
#include <optional>
#include <ranges>
#include <string>
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

// Drops panels this editor does not have. A closed panel stays closed until show() puts that one back. The Export
// panel is optional, so it is kept if present and not added if missing.
void drop_unknown(DockLayout& layout) {
    for (const std::string& key : layout.panels()) {
        if (key != EditorPanels::kExport &&
                std::ranges::find(EditorPanels::kKeys, key) == EditorPanels::kKeys.end()) {
            layout.remove(key);
        }
    }
}

// One closed panel, tabbed with the Profiler, else the first docked stack, else a new root. Other closed panels stay
// closed.
void place_closed(DockLayout& layout, std::string_view key) {
    if (key.empty() || layout.contains(key)) {
        return;
    }
    if (key != EditorPanels::kProfiler) {
        if (const std::optional<engine::ui::DockPanelPlace> profiler = layout.find(EditorPanels::kProfiler)) {
            if (layout.add(std::string(key), {profiler->stack, DockZone::Center})) {
                return;
            }
        }
    }
    const std::vector<DockNodeId> stacks = layout.stacks_under(layout.root());
    if (!stacks.empty() && layout.add(std::string(key), {stacks.front(), DockZone::Center})) {
        return;
    }
    layout.add(std::string(key), {});
}

// The saved layout when it holds at least one of the editor's panels, else the default.
DockLayout initial_layout(const DockLayoutFile& file) {
    std::optional<DockLayout> saved = file.load();
    const auto known = [&](std::string_view key) { return saved->contains(key); };
    if (!saved || std::ranges::none_of(EditorPanels::kKeys, known)) {
        return EditorPanels::default_layout();
    }
    drop_unknown(*saved);
    return std::move(*saved);
}

}

EditorPanels::EditorPanels(DockLayoutFile layout_file)
    : layout_file_(std::move(layout_file)),
      explorer_(selection_, commands_),
      explorer_input_(explorer_),
      hierarchy_(selection_),
      world_view_(selection_),
      ui_tree_(selection_),
      inspector_(selection_) {
    inspector_.set_journal(&commands_, [this] {
        world_view_.sync_entity_dirty(commands_.count(EditorCommand::Kind::Entity));
    });
    world_view_.set_journal(&commands_, [this] {
        world_view_.sync_entity_dirty(commands_.count(EditorCommand::Kind::Entity));
    });
    world_view_.set_edit_reset_hook([this] { commands_.discard(EditorCommand::Kind::Entity); });
}

DockLayout EditorPanels::default_layout() {
    DockLayout layout;
    layout.add(std::string(kProject), {});
    const DockNodeId project = layout.find(kProject)->stack;
    layout.add(std::string(kHierarchy), {project, DockZone::Center});
    layout.activate(kProject);
    layout.add(std::string(kInspector), {project, DockZone::Right});
    const DockNodeId inspector = layout.find(kInspector)->stack;
    layout.add(std::string(kWorld), {inspector, DockZone::Left});
    const DockNodeId tools = layout.find(kWorld)->stack;
    layout.add(std::string(kBuild), {tools, DockZone::Bottom});
    layout.add(std::string(kUiTree), {tools, DockZone::Center});
    layout.add(std::string(kProfiler), {tools, DockZone::Center});
    layout.activate(kUiTree);
    // Project and Hierarchy | the rest: a fifth of the width. Tools | Inspector: the Inspector gets 40% of the rest.
    // World, UI Tree, and Profiler over Build: Build gets 30%.
    layout.set_ratio(layout.root(), 0.2f);
    const DockNodeId column = layout.node(tools)->parent;
    layout.set_ratio(column, 0.7f);
    layout.set_ratio(layout.node(column)->parent, 0.6f);
    return layout;
}

void EditorPanels::spawn(engine::ecs::World& world, engine::WindowId window, engine::IWindowControl& windows) {
    world_ = &world;
    windows_ = &windows;
    window_ = window;
    explorer_canvas_ = spawn_panel(world, window, assets::ui::explorer, explorer_.view_model());
    hierarchy_canvas_ = spawn_panel(world, window, assets::ui::hierarchy, hierarchy_.view_model());
    world_canvas_ = spawn_panel(world, window, assets::ui::world_view, world_view_.view_model());
    ui_tree_canvas_ = spawn_panel(world, window, assets::ui::ui_tree, ui_tree_.view_model());
    inspector_canvas_ = spawn_panel(world, window, assets::ui::inspector, inspector_.view_model());
    profiler_canvas_ = spawn_panel(world, window, assets::ui::profiler, profiler_.view_model());
    build_canvas_ = spawn_panel(world, window, assets::ui::build, build_.view_model());
    export_canvas_ = spawn_panel(world, window, assets::ui::export_dialog, export_dialog_.view_model());

    engine::ui::DockSpace space;
    space.window = window;
    space.order = kDockOrder;
    // A floated panel gets an OS window of its own, bound to this world.
    space.float_mode = engine::ui::DockFloatMode::OsWindow;
    // The default chrome, then its editor overrides: transparent stack bodies so the World viewport shows through.
    space.stylesheets = {engine::builtin::dock_css, assets::css::dock};
    space.panels = {
            {std::string(kProject), "Project", explorer_canvas_, true},
            {std::string(kHierarchy), "Hierarchy", hierarchy_canvas_, true},
            {std::string(kWorld), "World", world_canvas_, true},
            {std::string(kUiTree), "UI Tree", ui_tree_canvas_, true},
            {std::string(kInspector), "Inspector", inspector_canvas_, true},
            {std::string(kProfiler), "Profiler", profiler_canvas_, true},
            {std::string(kBuild), "Build", build_canvas_, true},
            {std::string(kExport), "Export", export_canvas_, true},
    };
    space.layout = initial_layout(layout_file_);
    dock_ = world.create();
    saved_revision_ = space.revision;
    world.emplace<engine::ui::DockSpace>(dock_, std::move(space));
}

void EditorPanels::attach(engine::ecs::World& game) {
    hierarchy_.attach(game);
    ui_tree_.attach(game);
    inspector_.attach(game);
    profiler_.attach(game);
}

void EditorPanels::detach() {
    hierarchy_.detach();
    ui_tree_.detach();
    inspector_.detach();
    profiler_.detach();
}

void EditorPanels::frame(engine::ecs::World& world) {
    engine::ui::DockSpace& space = world.get<engine::ui::DockSpace>(dock_);
    const engine::ui::WindowSize size = engine::ui::window_size_for(world, window_);
    space.area = engine::render::Rect{0.0f, kToolbarHeight, static_cast<float>(size.width),
            std::max(0.0f, static_cast<float>(size.height) - kToolbarHeight)};
    explorer_input_.focus_rename(world, explorer_canvas_);
    read_tree_keys(world);
    explorer_input_.read_drag(world, explorer_canvas_);
    if (!explorer_.directory().empty()) {
        for (const ProjectRelocation& moved : explorer_.take_relocations()) {
            world_view_.retarget_open_file(explorer_.directory(), moved.from, moved.to);
        }
        for (const std::string& removed : explorer_.take_removals()) {
            world_view_.release_deleted_file(explorer_.directory(), removed);
        }
    }
    if (world_view_.sync_selection()) {
        show(kWorld);
    }
    for (const engine::ui::DockPanelCloseRequested& event :
            engine::ecs::EventReader<engine::ui::DockPanelCloseRequested>{world, close_cursor_}) {
        if (event.key == kExport) {
            close_export();
        } else if (std::ranges::find(kKeys, event.key) != kKeys.end()) {
            space.layout.remove(event.key);
        }
    }
    if (const std::optional<engine::WindowId> export_win = engine::ui::dock_panel_os_window(world, dock_, kExport)) {
        export_window_ = *export_win;
    }
    for (const engine::ui::WindowCloseRequestedEvent& event :
            engine::ecs::EventReader<engine::ui::WindowCloseRequestedEvent>{world, window_close_cursor_}) {
        if (export_window_ && event.window == *export_window_) {
            close_export();
        }
    }
    if (export_dialog_.take_close_requested()) {
        close_export();
    }
    engine::ecs::World* edit_world = world_view_.world_loaded() ? &world_view_.edit_world() : nullptr;
    const bool is_dirty = world_view_.is_dirty();
    hierarchy_.set_edit_world(edit_world, world_view_.open_path().filename().string(), is_dirty);
    for (engine::ui::DockPanel& panel : space.panels) {
        if (panel.key == kHierarchy) {
            panel.title = is_dirty ? "Hierarchy*" : "Hierarchy";
            break;
        }
    }
    inspector_.set_assets(world_view_.assets());
    inspector_.set_edit_world(edit_world);
    world_view_.frame(world, world_canvas_, space.layout.is_visible(kWorld));
    if (space.layout.is_visible(kHierarchy)) {
        hierarchy_.refresh();
    }
    if (space.layout.is_visible(kUiTree)) {
        ui_tree_.refresh();
    } else {
        ui_tree_.sync_selection();
    }
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

void EditorPanels::render(engine::ecs::World& world, const engine::EngineSystemDeps& deps) {
    const engine::ui::DockSpace& space = world.get<engine::ui::DockSpace>(dock_);
    if (space.layout.is_visible(kWorld)) {
        world_view_.render(world, world_canvas_, deps);
    }
}

void EditorPanels::show(std::string_view key) {
    DockLayout& layout = layout_mut();
    if (!layout.contains(key)) {
        place_closed(layout, key);
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

void EditorPanels::show_export(const std::filesystem::path& project, const std::string& target,
        const std::string& sdk_config) {
    export_dialog_.show(project, target, sdk_config);
    DockLayout& layout = layout_mut();
    const engine::ui::WindowSize size = engine::ui::window_size_for(*world_, window_);
    const float w = 520.0f;
    const float h = 260.0f;
    const float x = std::max(50.0f, (static_cast<float>(size.width) - w) * 0.5f);
    const float y = std::max(50.0f, (static_cast<float>(size.height) - h) * 0.5f);
    if (!layout.contains(kExport)) {
        const std::vector<engine::ui::DockNodeId> stacks = layout.stacks_under(layout.root());
        if (!stacks.empty()) {
            layout.add(std::string(kExport), engine::ui::DockTarget{stacks.front(), DockZone::Center});
        } else {
            layout.add(std::string(kExport), {});
        }
        layout.float_panel(kExport, engine::render::Rect{x, y, w, h});
    } else {
        const std::optional<engine::ui::DockPanelPlace> place = layout.find(kExport);
        if (place && place->float_id == engine::ui::kNoDockNode) {
            layout.float_panel(kExport, engine::render::Rect{x, y, w, h});
        }
    }
    show(kExport);
}

void EditorPanels::close_export() {
    export_window_.reset();
    export_dialog_.close();
    layout_mut().remove(kExport);
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
    if (key == kHierarchy) {
        return hierarchy_canvas_;
    }
    if (key == kWorld) {
        return world_canvas_;
    }
    if (key == kUiTree) {
        return ui_tree_canvas_;
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
    if (key == kExport) {
        return export_canvas_;
    }
    return {};
}

engine::ecs::Entity EditorPanels::tree_canvas_under_pointer(engine::ecs::World& world, engine::WindowId window) const {
    const engine::ecs::Entity top = top_canvas_at_pointer(world, window);
    return top == explorer_canvas_ || top == hierarchy_canvas_ || top == ui_tree_canvas_
            ? top
            : engine::ecs::Entity{};
}

void EditorPanels::read_tree_keys(engine::ecs::World& world) {
    // The tree under the pointer of the window the key went to: the editor window, or a panel's own float window.
    engine::ecs::Entity tree{};
    std::optional<std::size_t> keep_in_view;
    for (const engine::KeyEvent& event : engine::ecs::EventReader<engine::KeyEvent>{world, key_cursor_}) {
        if (event.key == engine::KeyCode::LCtrl || event.key == engine::KeyCode::RCtrl) {
            held_ctrl_ = event.down;
            continue;
        }
        if (event.key == engine::KeyCode::LShift || event.key == engine::KeyCode::RShift) {
            held_shift_ = event.down;
            continue;
        }
        if (!event.down) {
            continue;
        }
        if (held_ctrl_ && event.key == engine::KeyCode::Z) {
            if (held_shift_) {
                redo_edit();
            } else {
                undo_edit();
            }
            continue;
        }
        if (held_ctrl_ && !held_shift_ && event.key == engine::KeyCode::Y) {
            redo_edit();
            continue;
        }
        if (held_ctrl_ && event.key == engine::KeyCode::S) {
            (void) world_view_.save_world();
            continue;
        }
        if (!explorer_.renaming().empty()) {
            if (!event.repeat && event.key == engine::KeyCode::Escape) {
                explorer_.cancel_rename();
            } else if (!event.repeat && event.key == engine::KeyCode::Return) {
                explorer_.commit_rename();
            }
            continue;
        }
        const engine::ui::Element* focused = engine::ui::focused_element(world, event.window);
        if (focused != nullptr && focused->kind == engine::ui::ElementKind::TextInput) {
            continue;
        }
        const bool over_project = tree_canvas_under_pointer(world, event.window) == explorer_canvas_;
        if (over_project && explorer_input_.handle_key(event, held_ctrl_, held_shift_)) {
            continue;
        }
        const std::optional<engine::ui::TreeNav> nav = engine::ui::tree_nav_for_key(event.key);
        const engine::ecs::Entity under = nav ? tree_canvas_under_pointer(world, event.window) : engine::ecs::Entity{};
        if (under == engine::ecs::Entity{}) {
            continue;
        }
        tree = under;
        std::optional<std::size_t> row;
        if (tree == explorer_canvas_) {
            row = explorer_.navigate(*nav);
        } else if (tree == hierarchy_canvas_) {
            row = hierarchy_.navigate(*nav);
        } else {
            row = ui_tree_.navigate(*nav);
        }
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
        (void) engine::ui::scroll_item_into_view(instance->document, *scroller, *rows, *keep_in_view);
    }
}

void EditorPanels::undo_edit() {
    world_view_.finish_gizmo_drag();
    if (!commands_.undo()) {
        return;
    }
    world_view_.sync_entity_dirty(commands_.count(EditorCommand::Kind::Entity));
    inspector_.end_field_edit();
}

void EditorPanels::redo_edit() {
    world_view_.finish_gizmo_drag();
    if (!commands_.redo()) {
        return;
    }
    world_view_.sync_entity_dirty(commands_.count(EditorCommand::Kind::Entity));
    inspector_.end_field_edit();
}

const EditorSelection& EditorPanels::selection() const {
    return selection_;
}

ExplorerPanel& EditorPanels::explorer() {
    return explorer_;
}

HierarchyPanel& EditorPanels::hierarchy() {
    return hierarchy_;
}

WorldViewPanel& EditorPanels::world_view() {
    return world_view_;
}

UiTreePanel& EditorPanels::ui_tree() {
    return ui_tree_;
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

ExportDialog& EditorPanels::export_dialog() {
    return export_dialog_;
}

}
