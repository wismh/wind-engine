#pragma once

// docs/tech/features/UI Inspector.md

#include <engine/core/window_desc.h>
#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::ui {

    // One picked element. `path` is a child index per step, with `kGeneratedPathBit` set for a step
    // into Element::generated_items (see src/ui/element_path.h). `generated_owner` is the nearest
    // generated row on that path so a virtualized list can be found after its index moves. Empty
    // `path` is the canvas root. `active` is false when nothing is selected. `generated_owner` is
    // compared, never dereferenced outside the engine.
    struct InspectorPick {
        ecs::Entity canvas{};
        std::vector<std::size_t> path;
        const void *generated_owner = nullptr;
        bool active = false;
    };

    // One tree row across frames. A static element is its path from the canvas root. An element in a
    // generated row is its path from that row, so scrolling a virtualized list keeps the key. `owner`
    // is that row's generated_owner as a number (0 when there is none).
    struct InspectorRowKey {
        ecs::Entity canvas{};
        std::uintptr_t owner = 0;
        std::vector<std::size_t> relative;

        bool operator==(const InspectorRowKey &) const = default;
    };

    struct InspectorRowKeyHash {
        [[nodiscard]] std::size_t operator()(const InspectorRowKey &key) const noexcept;
    };

    // One row of inspector_tree. Plain data: nothing in it points into the live tree.
    struct InspectorTreeRow {
        InspectorRowKey key;
        // What selecting this row writes (`active` is true).
        InspectorPick pick;
        WindowId window = kPrimaryWindow;
        int depth = 0;
        // `Kind #id .class`, then ` spacer`, ` display:none`, or ` hidden`. The root row starts with
        // `[window] ` when canvases of the world sit on more than one window.
        std::string label;
        bool has_children = false;
        bool expanded = true;
        bool selected = false;
    };

    // The probe state, in the inspected world's ctx.
    struct UiInspector {
        // Pick, hover box, and selection work only while attached. The editor attaches the game world
        // on Play and detaches it on Stop.
        bool attached = false;
        // Left click on a canvas selects an element instead of reaching the game. Starts off; the
        // editor's Pick checkbox writes it.
        bool pick_pointer = false;
        // Which window's pick the detail shows. Updated on every pick and select.
        WindowId detail_window = kPrimaryWindow;
        std::unordered_map<WindowId, InspectorPick> selection;
        // Tree rows the user collapsed. Every other row is expanded.
        std::unordered_set<InspectorRowKey, InspectorRowKeyHash> collapsed;
    };

    // Attaching starts with no selection and pick off. Detaching clears the selection, the collapsed
    // rows, and pick. Does not bind a key, open a window, or spawn a canvas.
    void set_inspector_attached(ecs::World &world, bool attached);

    [[nodiscard]] bool inspector_attached(ecs::World &world);

    [[nodiscard]] InspectorPick inspector_selection(ecs::World &world, WindowId window = kPrimaryWindow);

    // Moves each selection's path to where its element is now (a rebuild or a scrolled virtualized
    // list moves it). run_ui_render calls it while attached, before it builds the overlay commands, and
    // inspector_tree calls it too.
    void inspector_retarget(ecs::World &world);

    // Every element of every canvas of `world`, depth first, skipping the children of a collapsed row.
    // Canvases are ordered by window, then `order`, then entity index. Empty when not attached.
    [[nodiscard]] std::vector<InspectorTreeRow> inspector_tree(ecs::World &world);

    // Selects `pick` (a row's pick) for `window` and shows it in the detail.
    void inspector_select(ecs::World &world, WindowId window, InspectorPick pick);

    // Collapses an expanded row or expands a collapsed one. A row without children stays as it is.
    void inspector_toggle(ecs::World &world, const InspectorRowKey &key);

    // Kind, id, classes, text, pseudo-classes, the three boxes, computed style from the last paint,
    // running animations, bound fields, and the rule count. A short line when `pick` is not active or
    // its element is gone.
    [[nodiscard]] std::string inspector_detail(ecs::World &world, const InspectorPick &pick);

    // The rules that match the picked element, lowest specificity first: `selector (specificity)`, then
    // one indented line per declaration. The last one ends its first line with ` winner`. `@media` uses
    // the size of the canvas's window.
    [[nodiscard]] std::vector<std::string> inspector_rules(ecs::World &world, const InspectorPick &pick);

    // Topmost canvas under the pointer on `window`. Null when the inspector is not attached or no canvas
    // contains the pointer.
    [[nodiscard]] std::optional<ecs::Entity> inspector_hover_canvas(ecs::World &world, WindowId window);

} // namespace engine::ui
