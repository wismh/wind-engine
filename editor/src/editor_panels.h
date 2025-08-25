#pragma once

#include "build_panel.h"
#include "dock_layout_file.h"
#include "editor_selection.h"
#include "explorer_panel.h"
#include "inspector_panel.h"
#include "profiler_panel.h"
#include "ui_tree_panel.h"

#include <engine/core/input_system.h>
#include <engine/core/window_control.h>
#include <engine/core/window_desc.h>
#include <engine/ecs/entity.h>
#include <engine/ecs/events.h>
#include <engine/ui/dock_layout.h>

#include <array>
#include <cstdint>
#include <string_view>

namespace engine::ecs {
class World;
}

namespace editor {

// The Project, UI Tree, Inspector, Profiler, and Build panels in an engine dock space that fills the editor window
// below the toolbar; a floated panel lives in an OS window of its own (DockFloatMode::OsWindow). Each panel is its own
// canvas in the editor's world; the dock space places it, so an inactive tab is a Fixed canvas with an empty rect and
// takes no clicks. The panels share one EditorSelection: the Project and UI Tree panels write it, the Inspector shows
// it. While playing, the UI Tree, Inspector, and Profiler read the world of kPrimaryWindow and refresh only while
// their tab is shown. The Build panel is filled by the editor as a build runs, and the Project panel scans when a
// project opens and on its Refresh. The layout is read from `layout_file` at spawn and written back when the user
// changes it and at quit.
class EditorPanels {
public:
    static constexpr std::string_view kProject = "project";
    static constexpr std::string_view kUiTree = "ui_tree";
    static constexpr std::string_view kInspector = "inspector";
    static constexpr std::string_view kProfiler = "profiler";
    static constexpr std::string_view kBuild = "build";
    // Dock order: the default layout's tabs and where reconcile puts a panel a saved layout lacks.
    static constexpr std::array<std::string_view, 5> kKeys{kProject, kUiTree, kInspector, kProfiler, kBuild};

    // Height of the toolbar in the editor window: `.toolbar` of assets/css/editor.css. The dock space is below it.
    static constexpr float kToolbarHeight = 56.0f;
    // Lowest canvas order of the dock space: above the editor's own canvas (order 0).
    static constexpr int kDockOrder = 1;

    explicit EditorPanels(DockLayoutFile layout_file);

    EditorPanels(const EditorPanels&) = delete;
    EditorPanels& operator=(const EditorPanels&) = delete;

    // Project on the left; in the middle UI Tree and Profiler tabbed (UI Tree shown) over Build; Inspector on the
    // right.
    [[nodiscard]] static engine::ui::DockLayout default_layout();

    // Spawns the panel canvases and the dock space in `world` for `window`, with the saved layout when it reads, else
    // the default. Called once, at editor start. `windows` raises a float's OS window in show().
    void spawn(engine::ecs::World& world, engine::WindowId window, engine::IWindowControl& windows);

    // Play, after the game started: attach the UI Tree, Inspector, and Profiler to `game`.
    void attach(engine::ecs::World& game);
    // First step of Stop: detach them, drop everything they copied from the game, and clear a UI element selection.
    void detach();

    // Editor world, Phase::Game (before Bind): keeps the dock area under the toolbar, gives tree keys to the Project
    // or UI Tree panel under the pointer, takes a new UI selection from the game (a pick click, UI Tree shown or not),
    // refreshes the UI Tree, Inspector, and Profiler while shown, so this frame's bindings see this frame's copy, and
    // saves the layout when the dock space changed it.
    void frame(engine::ecs::World& world);

    // Brings the panel to the front: its tab becomes active and its float, if any, goes on top; a float in an OS
    // window is raised and focused. A panel the layout lost goes back where reconcile puts it.
    void show(std::string_view key);
    // Writes the layout to the layout file. At quit.
    void save_layout();

    [[nodiscard]] const engine::ui::DockLayout& layout() const;
    [[nodiscard]] engine::ecs::Entity dock() const;
    [[nodiscard]] engine::ecs::Entity canvas(std::string_view key) const;

    [[nodiscard]] const EditorSelection& selection() const;
    [[nodiscard]] ExplorerPanel& explorer();
    [[nodiscard]] UiTreePanel& ui_tree();
    [[nodiscard]] InspectorPanel& inspector();
    [[nodiscard]] ProfilerPanel& profiler();
    [[nodiscard]] BuildPanel& build();

private:
    // Arrow, Home, and End presses (repeats too) on the editor window, or on a float's window, while the pointer is
    // over the Project or UI Tree panel there, whichever canvas is on top. Every frame reads the queue, so a key
    // pressed elsewhere is not replayed later.
    void read_tree_keys(engine::ecs::World& world);
    // The Project or UI Tree canvas when it is the topmost canvas of `window` under its pointer.
    [[nodiscard]] engine::ecs::Entity tree_canvas_under_pointer(engine::ecs::World& world,
            engine::WindowId window) const;
    [[nodiscard]] engine::ui::DockLayout& layout_mut();

    DockLayoutFile layout_file_;
    // Before the panels, which hold it.
    EditorSelection selection_;
    ExplorerPanel explorer_;
    UiTreePanel ui_tree_;
    InspectorPanel inspector_;
    ProfilerPanel profiler_;
    BuildPanel build_;
    engine::ecs::Entity explorer_canvas_{};
    engine::ecs::Entity ui_tree_canvas_{};
    engine::ecs::Entity inspector_canvas_{};
    engine::ecs::Entity profiler_canvas_{};
    engine::ecs::Entity build_canvas_{};
    engine::ecs::Entity dock_{};
    engine::ecs::World* world_ = nullptr;
    engine::IWindowControl* windows_ = nullptr;
    engine::WindowId window_{};
    // DockSpace::revision last written to the layout file.
    std::uint64_t saved_revision_ = 0;
    // Its own cursor: the UI's run_input reads KeyEvent through the world's.
    engine::ecs::EventCursor<engine::KeyEvent> key_cursor_;
};

}
