#pragma once

#include "inspector_row_view_model.h"
#include "inspector_view_model.h"
#include "rule_line_view_model.h"

#include <engine/ui/inspector.h>
#include <engine/ui/tree.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace engine::ecs {
class World;
}

namespace editor {

// The Inspector tab. Reads the engine's inspector probe of the game world (`engine/ui/inspector.h`) and
// shows it: the element tree, the computed block, and the matched rules. The tree and rule rows are
// plain copies; the panel keeps no Element or entity of the game past `detach`. Holds `this` in its rows,
// so it never moves.
class InspectorPanel {
public:
    InspectorPanel();

    InspectorPanel(const InspectorPanel&) = delete;
    InspectorPanel& operator=(const InspectorPanel&) = delete;

    [[nodiscard]] const std::shared_ptr<InspectorViewModel>& view_model() const;

    // Attaches the probe to `game` (the world of kPrimaryWindow). Pick starts off.
    void attach(engine::ecs::World& game);
    // Detaches the probe and clears every row. Must run before the game world is destroyed.
    void detach();
    [[nodiscard]] bool attached() const;

    // Copies the probe into the view-model. Pick is two-way: a checkbox click since the last refresh
    // writes UiInspector::pick_pointer, otherwise the game's value is shown.
    void refresh();

    void select(const engine::ui::InspectorTreeRow& row);
    void toggle(const engine::ui::InspectorRowKey& key);

    // A tree key on the shown rows, from the selected row of the detail's window. Selects and expands or
    // collapses in the game's probe; the rows show it on the next refresh. Returns the row to keep in view,
    // which is where that row sits after the refresh too.
    std::optional<std::size_t> navigate(engine::ui::TreeNav nav);

private:
    void show_rows();
    void show_selection();

    std::shared_ptr<InspectorViewModel> view_model_;
    engine::ecs::World* game_ = nullptr;
    // Row view-models by key, so a row keeps its element (hover, scroll) while the tree changes around it.
    std::unordered_map<engine::ui::InspectorRowKey, std::shared_ptr<InspectorRowViewModel>,
            engine::ui::InspectorRowKeyHash>
            rows_;
    std::vector<std::shared_ptr<RuleLineViewModel>> rules_;
    // The Pick value last shown in the view-model.
    bool pick_shown_ = false;
};

}
