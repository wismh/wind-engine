#pragma once

#include "build_panel.h"
#include "command_history.h"
#include "dock_layout_file.h"
#include "editor_selection.h"
#include "asset_picker_overlay.h"
#include "export_dialog.h" 
#include "explorer_input.h"
#include "explorer_panel.h"
#include "hierarchy_panel.h"
#include "inspector_panel.h"
#include "profiler_panel.h"
#include "ui_tree_panel.h"
#include "world_view_panel.h"

#include <engine/core/input_system.h>
#include <engine/core/window_control.h>
#include <engine/core/window_desc.h>
#include <engine/ecs/entity.h>
#include <engine/ecs/events.h>
#include <engine/ui/canvas.h>
#include <engine/ui/dock_layout.h>
#include <engine/ui/dock_space.h>

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace engine {
struct EngineSystemDeps;
}

namespace engine::ecs {
class World;
}

namespace editor {

// The Project, Hierarchy, World, UI Tree, Inspector, Profiler, and Build panels in an engine dock space that fills
// the editor window below the toolbar; a floated panel lives in an OS window of its own (DockFloatMode::OsWindow).
// The Export panel is hidden by default and opens in a separate OS window on File -> Export.
class EditorPanels {
public:
    static constexpr std::string_view kProject = "project";
    static constexpr std::string_view kHierarchy = "hierarchy";
    static constexpr std::string_view kWorld = "world";
    static constexpr std::string_view kUiTree = "ui_tree";
    static constexpr std::string_view kInspector = "inspector";
    static constexpr std::string_view kProfiler = "profiler";
    static constexpr std::string_view kBuild = "build";
    static constexpr std::string_view kExport = "export";
    // The seven panels Window can close and open again, in the default layout's tab order.
    static constexpr std::array<std::string_view, 7> kKeys{
            kProject, kHierarchy, kWorld, kUiTree, kInspector, kProfiler, kBuild};

    // Height of the titlebar + toolbar in the editor window. The dock space is below it.
    static constexpr float kToolbarHeight = 80.0f;
    // Lowest canvas order of the dock space: above the editor's own canvas (order 0).
    static constexpr int kDockOrder = 1;

    explicit EditorPanels(DockLayoutFile layout_file);

    EditorPanels(const EditorPanels&) = delete;
    EditorPanels& operator=(const EditorPanels&) = delete;

    // Project and Hierarchy on the left (Project shown); in the middle World, UI Tree, and Profiler tabbed (UI Tree
    // shown) over Build; Inspector on the right.
    [[nodiscard]] static engine::ui::DockLayout default_layout();

    // Spawns the panel canvases and the dock space in `world` for `window`, with the saved layout when it reads, else
    // the default. Called once, at editor start. `windows` raises a float's OS window in show().
    void spawn(engine::ecs::World& world, engine::WindowId window, engine::IWindowControl& windows);

    // Play, after the game started: attach the Hierarchy, UI Tree, Inspector, and Profiler to `game`.
    void attach(engine::ecs::World& game);
    // First step of Stop: detach them, drop everything they copied from the game, and clear a UI element or game
    // entity selection.
    void detach();

    void frame(engine::ecs::World& world);
    void render(engine::ecs::World& world, const engine::EngineSystemDeps& deps);

    // Brings the panel to the front: its tab becomes active and its float, if any, goes on top; a float in an OS
    // window is raised and focused. A panel the layout lost is tabbed with the Profiler, or the first docked stack
    // when the Profiler is closed too, or a new root when nothing is docked. Other closed panels stay closed.
    void show(std::string_view key);
    // Window menu. Each one is show() for that panel.
    void show_project() { show(kProject); }
    void show_hierarchy() { show(kHierarchy); }
    void show_world() { show(kWorld); }
    void show_ui_tree() { show(kUiTree); }
    void show_inspector() { show(kInspector); }
    void show_profiler() { show(kProfiler); }
    void show_build() { show(kBuild); }
    // Opens the Export panel in a separate floating OS window.
    void show_export(const std::filesystem::path& project, const std::string& target, const std::string& sdk_config);
    void close_export();

    // Writes the layout to the layout file. At quit.
    void save_layout();

    [[nodiscard]] const engine::ui::DockLayout& layout() const;
    [[nodiscard]] engine::ecs::Entity dock() const;
    [[nodiscard]] engine::ecs::Entity canvas(std::string_view key) const;

    [[nodiscard]] const EditorSelection& selection() const;
    [[nodiscard]] ExplorerPanel& explorer();
    [[nodiscard]] HierarchyPanel& hierarchy();
    [[nodiscard]] WorldViewPanel& world_view();
    [[nodiscard]] UiTreePanel& ui_tree();
    [[nodiscard]] InspectorPanel& inspector();
    [[nodiscard]] ProfilerPanel& profiler();
    [[nodiscard]] BuildPanel& build();
    [[nodiscard]] ExportDialog& export_dialog();
    [[nodiscard]] AssetPickerOverlay& asset_picker() noexcept { return asset_picker_; }

    void undo_edit();
    void redo_edit();
    [[nodiscard]] bool can_undo_edit() const { return commands_.can_undo(); }
    [[nodiscard]] bool can_redo_edit() const { return commands_.can_redo(); }

private:
    void read_tree_keys(engine::ecs::World& world);
    // Column count from the laid-out first line, and the grid scroller: top after a folder change, then the
    // selected tile once it has a rect. `scroll_item_into_view` assumes one column, so the grid does not use it.
    void sync_project_grid(engine::ecs::World& world);
    [[nodiscard]] engine::ecs::Entity tree_canvas_under_pointer(engine::ecs::World& world,
            engine::WindowId window) const;
    [[nodiscard]] engine::ui::DockLayout& layout_mut();

    CommandHistory commands_;
    DockLayoutFile layout_file_;
    EditorSelection selection_;
    ExplorerPanel explorer_;
    ExplorerInput explorer_input_;
    HierarchyPanel hierarchy_;
    WorldViewPanel world_view_;
    UiTreePanel ui_tree_;
    InspectorPanel inspector_;
    ProfilerPanel profiler_;
    BuildPanel build_;
    ExportDialog export_dialog_;
    AssetPickerOverlay asset_picker_;
    engine::ecs::Entity explorer_canvas_{};
    engine::ecs::Entity hierarchy_canvas_{};
    engine::ecs::Entity world_canvas_{};
    engine::ecs::Entity ui_tree_canvas_{};
    engine::ecs::Entity inspector_canvas_{};
    engine::ecs::Entity profiler_canvas_{};
    engine::ecs::Entity build_canvas_{};
    engine::ecs::Entity export_canvas_{};
    engine::ecs::Entity dock_{};
    engine::ecs::World* world_ = nullptr;
    engine::IWindowControl* windows_ = nullptr;
    engine::WindowId window_{};
    std::uint64_t saved_revision_ = 0;
    std::optional<engine::WindowId> export_window_{};
    engine::ecs::EventCursor<engine::KeyEvent> key_cursor_;
    engine::ecs::EventCursor<engine::ui::DockPanelCloseRequested> close_cursor_;
    engine::ecs::EventCursor<engine::ui::WindowCloseRequestedEvent> window_close_cursor_;
    bool held_ctrl_ = false;
    bool held_shift_ = false;
};

}
