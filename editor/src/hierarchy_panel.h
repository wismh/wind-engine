#pragma once

#include "editor_selection.h"
#include "hierarchy_row_view_model.h"
#include "hierarchy_view_model.h"

#include <engine/ecs/entity.h>
#include <engine/ui/tree.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::ecs {
class World;
}

namespace editor {

// The Hierarchy tab (`assets/ui/hierarchy.xml`): shows the entity hierarchy tree of the active ECS world
// (the live game world during Play, or the `.world` preview world in Edit Mode). Selecting a row publishes an
// `EntitySelection` to `EditorSelection` so the Inspector shows that entity and its components. `refresh` adopts
// an entity selection made elsewhere (a World-tab click) and drops the highlight when the selection is cleared.
// Holds `this` in its rows, so it never moves.
class HierarchyPanel {
public:
    explicit HierarchyPanel(EditorSelection& selection);

    HierarchyPanel(const HierarchyPanel&) = delete;
    HierarchyPanel& operator=(const HierarchyPanel&) = delete;

    [[nodiscard]] const std::shared_ptr<HierarchyViewModel>& view_model() const;

    // Play Mode: attach to the live game world (`kPrimaryWindow`).
    void attach(engine::ecs::World& game);
    void detach();
    [[nodiscard]] bool attached() const;

    // Edit Mode: set the preview world loaded by `WorldViewPanel` (or nullptr when no `.world` is open).
    void set_edit_world(engine::ecs::World* edit_world, std::string_view world_label = {}, bool is_dirty = false);
    [[nodiscard]] bool is_dirty() const noexcept { return is_dirty_; }

    // Rebuilds the visible hierarchy rows from the active world while the tab is shown.
    void refresh();

    void select(engine::ecs::Entity entity);
    void toggle(engine::ecs::Entity entity);
    std::optional<std::size_t> navigate(engine::ui::TreeNav nav);

    [[nodiscard]] engine::ecs::Entity selected() const noexcept { return selected_; }

private:
    [[nodiscard]] const engine::ecs::World* active_world() const noexcept;
    void show_rows(const engine::ecs::World& world);
    void clear_selection_if_entity();

    EditorSelection* selection_;
    std::shared_ptr<HierarchyViewModel> view_model_;
    engine::ecs::World* game_ = nullptr;
    engine::ecs::World* edit_world_ = nullptr;
    std::string edit_label_;
    bool is_dirty_ = false;
    engine::ui::TreeExpansion<std::uint32_t> expansion_{true};
    engine::ecs::Entity selected_{};
    // Row view-models keyed by entity index so hover/scroll state is kept across refreshes.
    std::unordered_map<std::uint32_t, std::shared_ptr<HierarchyRowViewModel>> rows_;
};

}
