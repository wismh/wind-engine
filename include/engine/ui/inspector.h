#pragma once

#include <engine/core/window_desc.h>
#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

namespace engine::ui {

    // One picked element. `path` is a child index per step, with `kGeneratedPathBit` set for a step
    // into Element::generated_items (see src/ui/element_path.h). `generated_owner` is the nearest
    // generated row on that path so a virtualized list can be found after its index moves. Empty
    // `path` is the canvas root. `active` is false when nothing is selected.
    struct InspectorPick {
        ecs::Entity canvas{};
        std::vector<std::size_t> path;
        const void *generated_owner = nullptr;
        bool active = false;
    };

    struct UiInspector {
        bool enabled = false;
        std::unordered_map<WindowId, InspectorPick> selection;
    };

    // Marks the inspector's own canvas so pick, the tree, and the hover box skip it.
    struct InspectorPanel {
        WindowId window = kPrimaryWindow;
    };

    // Spawns or destroys the per-window panel. Does not bind a key; the game calls this.
    void set_inspector_enabled(ecs::World &world, bool enabled);

    [[nodiscard]] bool inspector_enabled(ecs::World &world);

    [[nodiscard]] InspectorPick inspector_selection(ecs::World &world, WindowId window = kPrimaryWindow);

    // Places one Fixed panel per live window. Called from begin_frame.
    void sync_inspector_frames(ecs::World &world);

    // Rebuilds the panel's tree and the selected element's text. Called at the start of Bind,
    // before run_bind, and from set_inspector_enabled.
    void sync_inspector_content(ecs::World &world);

    // Topmost non-panel canvas under the pointer. Null when the inspector is off, the pointer is
    // over the panel, or no canvas contains it.
    [[nodiscard]] std::optional<ecs::Entity> inspector_hover_canvas(ecs::World &world, WindowId window);

} // namespace engine::ui
