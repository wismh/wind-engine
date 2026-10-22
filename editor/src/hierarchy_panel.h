#pragma once

#include "command_history.h"
#include "editor_selection.h"
#include "hierarchy_row_view_model.h"
#include "hierarchy_view_model.h"

#include <engine/ecs/entity.h>
#include <engine/ui/tree.h>

#include <cstddef>
#include <cstdint>
#include <functional>
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

    void set_command_history(CommandHistory* journal) noexcept { journal_ = journal; }
    void set_on_history(std::function<void()> cb) { on_history_ = std::move(cb); }

    // Rebuilds the visible hierarchy rows from the active world while the tab is shown.
    void refresh();

    void select(engine::ecs::Entity entity);
    void toggle(engine::ecs::Entity entity);
    std::optional<std::size_t> navigate(engine::ui::TreeNav nav);

    [[nodiscard]] engine::ecs::Entity selected() const noexcept { return selected_; }

    // Mutating actions (Edit Mode only)
    void create_entity(std::optional<engine::ecs::Entity> parent = std::nullopt);
    void remove_selected();
    void remove_entity(engine::ecs::Entity entity);
    void begin_rename(engine::ecs::Entity entity = {});
    void commit_rename();
    void cancel_rename();
    [[nodiscard]] engine::ecs::Entity renaming() const noexcept { return renaming_; }
    [[nodiscard]] bool rename_focus_pending() const noexcept { return rename_focus_pending_; }
    void rename_focused() noexcept { rename_focus_pending_ = false; }

    // Drag & Drop
    [[nodiscard]] bool can_drop(engine::ecs::Entity source, std::optional<engine::ecs::Entity> target) const;
    void set_drop_highlight(std::optional<engine::ecs::Entity> target);
    void reparent(engine::ecs::Entity child, std::optional<engine::ecs::Entity> new_parent);

    [[nodiscard]] std::optional<engine::ecs::Entity> entity_for(const engine::ui::ViewModel* vm) const;

private:
    void create_root_entity();
    [[nodiscard]] const engine::ecs::World* active_world() const noexcept;
    void show_rows(const engine::ecs::World& world);
    void clear_selection_if_entity();
    void note_history();

    EditorSelection* selection_;
    CommandHistory* journal_ = nullptr;
    std::function<void()> on_history_;
    std::shared_ptr<HierarchyViewModel> view_model_;
    engine::ecs::World* game_ = nullptr;
    engine::ecs::World* edit_world_ = nullptr;
    std::string edit_label_;
    bool is_dirty_ = false;
    engine::ui::TreeExpansion<std::uint32_t> expansion_{true};
    engine::ecs::Entity selected_{};
    engine::ecs::Entity renaming_{};
    bool rename_focus_pending_ = false;
    std::optional<engine::ecs::Entity> drop_entity_;
    // Row view-models keyed by entity index so hover/scroll state is kept across refreshes.
    std::unordered_map<std::uint32_t, std::shared_ptr<HierarchyRowViewModel>> rows_;
};

}
