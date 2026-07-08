#pragma once

#include "build_panel.h"
#include "editor_tab.h"
#include "explorer_panel.h"
#include "inspector_panel.h"
#include "profiler_panel.h"

#include <engine/core/input_system.h>
#include <engine/core/window_desc.h>
#include <engine/ecs/entity.h>
#include <engine/ecs/events.h>
#include <engine/render/commands.h>

namespace engine::ecs {
class World;
}

namespace editor {

class Toolbar;

// The Project, Inspector, Profiler, and Build tabs. Each panel is its own canvas in the editor's world, under the
// toolbar and the tab strip; an inactive one is a Fixed canvas with an empty rect, so it takes no clicks.
// While playing, the Inspector and Profiler are attached to the world of kPrimaryWindow; only the visible one
// refreshes. The Build panel is filled by the editor as a build runs, and the Project panel scans when a project
// opens and on its Refresh.
class EditorPanels {
public:
    // y of the panels in the editor window: the toolbar (56) and the tab strip (32) of
    // assets/css/editor.css.
    static constexpr float kPanelTop = 88.0f;

    explicit EditorPanels(const Toolbar& toolbar);

    EditorPanels(const EditorPanels&) = delete;
    EditorPanels& operator=(const EditorPanels&) = delete;

    // Spawns the panel canvases in `world` for `window`. Called once, at editor start.
    void spawn(engine::ecs::World& world, engine::WindowId window);

    // Play, after the game started: attach the Inspector and Profiler to `game`.
    void attach(engine::ecs::World& game);
    // First step of Stop: detach the Inspector and Profiler and drop everything they copied from the game.
    void detach();

    // Editor world, Phase::Game (before Bind): places the canvases for the active tab, gives tree keys to
    // the Project or Inspector tab while the pointer is over it, and refreshes the visible panel, so this
    // frame's bindings see this frame's copy.
    void frame(engine::ecs::World& world);

    [[nodiscard]] ExplorerPanel& explorer();
    [[nodiscard]] InspectorPanel& inspector();
    [[nodiscard]] ProfilerPanel& profiler();
    [[nodiscard]] BuildPanel& build();

private:
    // Arrow, Home, and End presses (repeats too) on the editor window while the pointer is inside `panel` and
    // `tab` is a tree. Every frame reads the queue, so a key pressed elsewhere is not replayed later.
    void read_tree_keys(engine::ecs::World& world, const engine::render::Rect& panel, EditorTab tab);

    const Toolbar* toolbar_;
    ExplorerPanel explorer_;
    InspectorPanel inspector_;
    ProfilerPanel profiler_;
    BuildPanel build_;
    engine::ecs::Entity explorer_canvas_{};
    engine::ecs::Entity inspector_canvas_{};
    engine::ecs::Entity profiler_canvas_{};
    engine::ecs::Entity build_canvas_{};
    engine::WindowId window_{};
    // Its own cursor: the UI's run_input reads KeyEvent through the world's.
    engine::ecs::EventCursor<engine::KeyEvent> key_cursor_;
};

}
