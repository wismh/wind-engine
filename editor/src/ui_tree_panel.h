#pragma once

#include "editor_selection.h"
#include "ui_tree_row_view_model.h"
#include "ui_tree_view_model.h"

#include <engine/ui/inspector.h>
#include <engine/ui/tree.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

namespace engine::ecs {
class World;
}

namespace editor {

// The UI Tree tab. Reads the engine's inspector probe of the game world (`engine/ui/inspector.h`) and shows its
// element tree and the Pick switch. A new selection in the probe, from a row or a pick click in the game, becomes
// the editor's selection, which the Inspector tab shows. The rows are plain copies; the panel keeps no Element or
// entity of the game past `detach`. Holds `this` in its rows, so it never moves.
class UiTreePanel {
public:
    explicit UiTreePanel(EditorSelection& selection);

    UiTreePanel(const UiTreePanel&) = delete;
    UiTreePanel& operator=(const UiTreePanel&) = delete;

    [[nodiscard]] const std::shared_ptr<UiTreeViewModel>& view_model() const;

    // Attaches the probe to `game` (the world of kPrimaryWindow). Pick starts off.
    void attach(engine::ecs::World& game);
    // Detaches the probe, clears every row, and clears the editor's selection when it is a UI element. Must run
    // before the game world is destroyed.
    void detach();
    [[nodiscard]] bool attached() const;

    // Every frame while attached, shown or not: a selection the probe made since the last call (a row, a tree
    // key, a pick click) becomes the editor's selection. A retarget is not a new selection.
    void sync_selection();
    // While the tab is shown. Copies the tree into the view-model and syncs the selection. Pick is two-way: a
    // checkbox click since the last refresh writes UiInspector::pick_pointer, otherwise the game's value is shown.
    void refresh();

    void select(const engine::ui::InspectorTreeRow& row);
    void toggle(const engine::ui::InspectorRowKey& key);

    // A tree key on the shown rows, from the selected row of the detail's window. Selects and expands or
    // collapses in the game's probe; the rows show it on the next refresh. Returns the row to keep in view,
    // which is where that row sits after the refresh too.
    std::optional<std::size_t> navigate(engine::ui::TreeNav nav);

private:
    void show_rows();

    EditorSelection* selection_;
    std::shared_ptr<UiTreeViewModel> view_model_;
    engine::ecs::World* game_ = nullptr;
    // Row view-models by key, so a row keeps its element (hover, scroll) while the tree changes around it.
    std::unordered_map<engine::ui::InspectorRowKey, std::shared_ptr<UiTreeRowViewModel>,
            engine::ui::InspectorRowKeyHash>
            rows_;
    // The Pick value last shown in the view-model.
    bool pick_shown_ = false;
    // UiInspector::selections at the last sync_selection.
    std::uint64_t selections_seen_ = 0;
};

}
