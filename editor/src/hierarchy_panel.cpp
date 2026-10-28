#include "hierarchy_panel.h"

#include "entity_inspection.h"
#include "hierarchy_commands.h"

#include <engine/ecs/camera.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_io.h>
#include <engine/ecs/world_serializer.h>
#include <engine/resources/assets_db.h>

#include <algorithm>
#include <format>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace editor {
namespace {

constexpr char kIdleHint[] = "Open a .world file in Project, or press Play.";

struct EntityTreeSource {
    const engine::ecs::World* world = nullptr;
    const std::vector<engine::ecs::Entity>* alive = nullptr;

    [[nodiscard]] std::uint32_t key(engine::ecs::Entity entity) const { return entity.index; }

    [[nodiscard]] std::vector<engine::ecs::Entity> children_of(engine::ecs::Entity entity) const {
        std::vector<engine::ecs::Entity> out;
        if (const auto* children = world->try_get<engine::Children>(entity)) {
            for (const engine::ecs::Entity child : children->entities) {
                if (world->valid(child)) {
                    out.push_back(child);
                }
            }
        }
        for (const engine::ecs::Entity other : *alive) {
            const auto* parent = world->try_get<engine::Parent>(other);
            if (parent != nullptr && parent->entity == entity &&
                    std::ranges::find(out, other) == out.end()) {
                out.push_back(other);
            }
        }
        return out;
    }

    [[nodiscard]] bool has_children(engine::ecs::Entity entity) const {
        return !children_of(entity).empty();
    }

    template<typename Visit>
    void for_each_child(engine::ecs::Entity entity, Visit&& visit) const {
        for (const engine::ecs::Entity child : children_of(entity)) {
            visit(child);
        }
    }
};

std::string format_row_label(const engine::ecs::World& world, engine::ecs::Entity entity) {
    std::string label = entity_display_name(world, entity);
    if (const auto* active_cam = world.try_ctx<engine::ecs::ActiveCamera>();
            active_cam != nullptr && active_cam->entity == entity) {
        label += "  [ActiveCamera]";
    } else if (world.try_get<engine::ecs::Camera>(entity) != nullptr) {
        label += "  [Camera]";
    }
    return label;
}

bool is_descendant(const engine::ecs::World& world, engine::ecs::Entity potential_child, engine::ecs::Entity ancestor) {
    engine::ecs::Entity curr = potential_child;
    while (world.valid(curr)) {
        const auto* parent = world.try_get<engine::Parent>(curr);
        if (parent == nullptr || !world.valid(parent->entity)) {
            break;
        }
        if (parent->entity == ancestor) {
            return true;
        }
        curr = parent->entity;
    }
    return false;
}

}

HierarchyPanel::HierarchyPanel(EditorSelection& selection)
    : selection_(&selection), view_model_(std::make_shared<HierarchyViewModel>()) {
    view_model_->statusText = std::string(kIdleHint);
    view_model_->createEntity.bind_to<HierarchyPanel, &HierarchyPanel::create_root_entity>(*this);
}

const std::shared_ptr<HierarchyViewModel>& HierarchyPanel::view_model() const {
    return view_model_;
}

void HierarchyPanel::attach(engine::ecs::World& game) {
    game_ = &game;
    expansion_.reset();
    rows_.clear();
    renaming_ = {};
    rename_focus_pending_ = false;
    drop_entity_.reset();
    clear_selection_if_entity();
}

void HierarchyPanel::detach() {
    game_ = nullptr;
    expansion_.reset();
    rows_.clear();
    renaming_ = {};
    rename_focus_pending_ = false;
    drop_entity_.reset();
    view_model_->rows.set({});
    clear_selection_if_entity();
    if (edit_world_ == nullptr) {
        view_model_->statusText = std::string(kIdleHint);
    }
}

bool HierarchyPanel::attached() const {
    return game_ != nullptr;
}

void HierarchyPanel::set_edit_world(engine::ecs::World* edit_world, std::string_view world_label, bool is_dirty) {
    const bool world_identity_changed = edit_world != edit_world_ || edit_label_ != world_label;
    const bool dirty_changed = is_dirty != is_dirty_;
    edit_world_ = edit_world;
    edit_label_ = std::string(world_label);
    is_dirty_ = is_dirty;
    if (game_ == nullptr) {
        if (world_identity_changed || (selected_ != engine::ecs::Entity{} &&
                (edit_world_ == nullptr || !edit_world_->valid(selected_)))) {
            clear_selection_if_entity();
        }
        if (world_identity_changed) {
            expansion_.reset();
            rows_.clear();
            renaming_ = {};
            rename_focus_pending_ = false;
            drop_entity_.reset();
            if (edit_world_ == nullptr) {
                view_model_->rows.set({});
                view_model_->statusText = std::string(kIdleHint);
            }
        } else if (dirty_changed && edit_world_ != nullptr) {
            const std::string_view prefix =
                    edit_label_.empty() ? std::string_view("Scene") : std::string_view(edit_label_);
            const std::string star = is_dirty_ ? "*" : "";
            const std::vector<engine::ecs::Entity> alive = edit_world_->alive_entities();
            view_model_->statusText = std::format("{}{}  ({} entities)", prefix, star, alive.size());
        }
    }
}

void HierarchyPanel::refresh() {
    const engine::ecs::World* world = active_world();
    if (world == nullptr) {
        clear_selection_if_entity();
        rows_.clear();
        renaming_ = {};
        rename_focus_pending_ = false;
        drop_entity_.reset();
        view_model_->rows.set({});
        view_model_->statusText = std::string(kIdleHint);
        return;
    }

    if (const auto* ent_sel = std::get_if<EntitySelection>(&selection_->target())) {
        if (!world->valid(ent_sel->entity)) {
            selection_->clear();
            selected_ = {};
        } else {
            selected_ = ent_sel->entity;
        }
    } else if (std::holds_alternative<std::monostate>(selection_->target())) {
        selected_ = {};
    } else if (selected_ != engine::ecs::Entity{} && !world->valid(selected_)) {
        selected_ = {};
    }

    if (renaming_ != engine::ecs::Entity{} && !world->valid(renaming_)) {
        renaming_ = {};
        rename_focus_pending_ = false;
    }

    show_rows(*world);
}

void HierarchyPanel::select(engine::ecs::Entity entity) {
    const engine::ecs::World* world = active_world();
    if (world == nullptr || !world->valid(entity)) {
        return;
    }
    selected_ = entity;
    show_rows(*world);
    selection_->select(EntitySelection{.entity = entity});
}

void HierarchyPanel::toggle(engine::ecs::Entity entity) {
    const engine::ecs::World* world = active_world();
    if (world == nullptr || !world->valid(entity)) {
        return;
    }
    const std::vector<engine::ecs::Entity> alive = world->alive_entities();
    const EntityTreeSource source{.world = world, .alive = &alive};
    if (!source.has_children(entity)) {
        return;
    }
    expansion_.toggle(entity.index);
    show_rows(*world);
}

std::optional<std::size_t> HierarchyPanel::navigate(engine::ui::TreeNav nav) {
    if (active_world() == nullptr) {
        return std::nullopt;
    }
    const std::vector<std::shared_ptr<HierarchyRowViewModel>>& rows = view_model_->rows.get();
    std::vector<engine::ui::TreeRowInfo> infos;
    infos.reserve(rows.size());
    std::size_t current = engine::ui::kNoTreeRow;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        infos.push_back(rows[i]->tree());
        if (rows[i]->entity() == selected_) {
            current = i;
        }
    }
    const engine::ui::TreeNavResult result = engine::ui::tree_navigate(infos, current, nav);
    if (result.row == engine::ui::kNoTreeRow) {
        return std::nullopt;
    }
    const engine::ecs::Entity target = rows[result.row]->entity();
    if (result.toggle) {
        toggle(target);
    }
    if (result.row != current) {
        select(target);
    }
    return result.row;
}

void HierarchyPanel::create_root_entity() {
    create_entity(std::nullopt);
}

void HierarchyPanel::create_entity(std::optional<engine::ecs::Entity> parent) {
    engine::ecs::World* world = edit_world_;
    if (world == nullptr || game_ != nullptr) {
        return;
    }
    if (parent && !world->valid(*parent)) {
        parent = std::nullopt;
    }

    std::string base_name = parent ? "Child" : "Entity";
    const std::vector<engine::ecs::Entity> alive = world->alive_entities();
    std::unordered_set<std::string> names;
    for (const engine::ecs::Entity e : alive) {
        if (const auto* name = world->try_get<engine::ecs::EntityName>(e)) {
            names.insert(name->id);
        }
    }
    std::string chosen = base_name;
    int counter = 1;
    while (names.contains(chosen)) {
        chosen = std::format("{} ({})", base_name, counter++);
    }

    const engine::ecs::Entity entity = world->create();
    world->emplace<engine::ecs::EntityName>(entity, engine::ecs::EntityName{.id = chosen});
    world->emplace<engine::Transform>(entity, engine::Transform{});

    if (parent) {
        (void)engine::ecs::set_parent(*world, entity, *parent, engine::ecs::KeepWorld::No);
        expansion_.set_expanded(parent->index, true);
    }

    const auto* serializer = world->try_ctx<engine::ecs::WorldSerializer>();
    auto* assets = world->try_ctx<engine::AssetsDb>();
    if (journal_ != nullptr) {
        journal_->push(std::make_unique<EntityCreateCommand>(world, entity, parent, serializer, assets));
    }
    note_history();

    select(entity);
    begin_rename(entity);
}

void HierarchyPanel::remove_selected() {
    if (selected_ != engine::ecs::Entity{}) {
        remove_entity(selected_);
    }
}

void HierarchyPanel::remove_entity(engine::ecs::Entity entity) {
    engine::ecs::World* world = edit_world_;
    if (world == nullptr || game_ != nullptr || !world->valid(entity)) {
        return;
    }
    if (renaming_ == entity) {
        cancel_rename();
    }
    std::optional<engine::ecs::Entity> parent;
    if (const auto* p = world->try_get<engine::Parent>(entity); p != nullptr && world->valid(p->entity)) {
        parent = p->entity;
    }
    const auto* serializer = world->try_ctx<engine::ecs::WorldSerializer>();
    auto* assets = world->try_ctx<engine::AssetsDb>();

    if (selected_ == entity || is_descendant(*world, selected_, entity)) {
        clear_selection_if_entity();
    }

    if (journal_ != nullptr) {
        journal_->push(std::make_unique<EntityDeleteCommand>(world, entity, parent, serializer, assets));
    } else {
        destroy_subtree(*world, entity);
    }
    note_history();
    refresh();
}

void HierarchyPanel::begin_rename(engine::ecs::Entity entity) {
    if (entity == engine::ecs::Entity{}) {
        entity = selected_;
    }
    const engine::ecs::World* world = active_world();
    if (world == nullptr || game_ != nullptr || !world->valid(entity)) {
        return;
    }
    renaming_ = entity;
    rename_focus_pending_ = true;
    show_rows(*world);
    if (const auto it = rows_.find(entity.index); it != rows_.end()) {
        std::string cur_name;
        if (const auto* name = world->try_get<engine::ecs::EntityName>(entity)) {
            cur_name = name->id;
        } else {
            cur_name = std::format("entity_{}", entity.index);
        }
        it->second->seed_edit(cur_name);
    }
}

void HierarchyPanel::cancel_rename() {
    if (renaming_ == engine::ecs::Entity{}) {
        return;
    }
    renaming_ = {};
    rename_focus_pending_ = false;
    refresh();
}

void HierarchyPanel::commit_rename() {
    if (renaming_ == engine::ecs::Entity{}) {
        return;
    }
    engine::ecs::World* world = edit_world_;
    const engine::ecs::Entity entity = renaming_;
    renaming_ = {};
    rename_focus_pending_ = false;
    if (world == nullptr || game_ != nullptr || !world->valid(entity)) {
        refresh();
        return;
    }
    const auto it = rows_.find(entity.index);
    if (it == rows_.end()) {
        refresh();
        return;
    }
    std::string new_name = it->second->editText.get();
    while (!new_name.empty() && (new_name.front() == ' ' || new_name.front() == '\t')) {
        new_name.erase(new_name.begin());
    }
    while (!new_name.empty() && (new_name.back() == ' ' || new_name.back() == '\t')) {
        new_name.pop_back();
    }
    std::string old_name;
    if (const auto* name = world->try_get<engine::ecs::EntityName>(entity)) {
        old_name = name->id;
    }
    if (new_name == old_name || new_name.empty()) {
        refresh();
        return;
    }
    if (auto* name = world->try_get<engine::ecs::EntityName>(entity)) {
        name->id = new_name;
    } else {
        world->emplace<engine::ecs::EntityName>(entity, engine::ecs::EntityName{.id = new_name});
    }
    if (journal_ != nullptr) {
        journal_->push(std::make_unique<EntityRenameCommand>(world, entity, old_name, new_name));
    }
    note_history();
    refresh();
}

bool HierarchyPanel::can_drop(engine::ecs::Entity source, std::optional<engine::ecs::Entity> target) const {
    const engine::ecs::World* world = active_world();
    if (world == nullptr || game_ != nullptr || !world->valid(source)) {
        return false;
    }
    if (!target) {
        const auto* parent = world->try_get<engine::Parent>(source);
        return parent != nullptr && world->valid(parent->entity);
    }
    if (*target == source || !world->valid(*target)) {
        return false;
    }
    const auto* parent = world->try_get<engine::Parent>(source);
    if (parent != nullptr && parent->entity == *target) {
        return false;
    }
    if (is_descendant(*world, *target, source)) {
        return false;
    }
    return true;
}

void HierarchyPanel::set_drop_highlight(std::optional<engine::ecs::Entity> target) {
    if (drop_entity_ == target) {
        return;
    }
    drop_entity_ = target;
    for (const auto& [idx, row] : rows_) {
        const bool is_selected = row->entity() == selected_;
        const bool is_drop = drop_entity_ && row->entity() == *drop_entity_;
        row->set_fill(is_selected, is_drop);
    }
}

void HierarchyPanel::reparent(engine::ecs::Entity child, std::optional<engine::ecs::Entity> new_parent) {
    if (!can_drop(child, new_parent)) {
        return;
    }
    engine::ecs::World* world = edit_world_;
    if (world == nullptr || game_ != nullptr) {
        return;
    }
    std::optional<engine::ecs::Entity> old_parent;
    if (const auto* p = world->try_get<engine::Parent>(child); p != nullptr && world->valid(p->entity)) {
        old_parent = p->entity;
    }
    std::optional<engine::Transform> old_transform;
    if (const auto* tr = world->try_get<engine::Transform>(child)) {
        old_transform = *tr;
    }

    if (new_parent) {
        if (!engine::ecs::set_parent(*world, child, *new_parent, engine::ecs::KeepWorld::Yes)) {
            return;
        }
    } else {
        engine::ecs::clear_parent(*world, child, engine::ecs::KeepWorld::Yes);
    }

    std::optional<engine::Transform> new_transform;
    if (const auto* tr = world->try_get<engine::Transform>(child)) {
        new_transform = *tr;
    }

    if (journal_ != nullptr) {
        journal_->push(std::make_unique<EntityReparentCommand>(
                world, child, old_parent, new_parent, old_transform, new_transform));
    }
    note_history();
    if (new_parent) {
        expansion_.set_expanded(new_parent->index, true);
    }
    select(child);
}

std::optional<engine::ecs::Entity> HierarchyPanel::entity_for(const engine::ui::ViewModel* vm) const {
    for (const auto& [idx, row] : rows_) {
        if (row.get() == vm) {
            return row->entity();
        }
    }
    return std::nullopt;
}

const engine::ecs::World* HierarchyPanel::active_world() const noexcept {
    return game_ != nullptr ? game_ : edit_world_;
}

void HierarchyPanel::show_rows(const engine::ecs::World& world) {
    const std::vector<engine::ecs::Entity> alive = world.alive_entities();
    std::unordered_set<std::uint32_t> alive_indices;
    alive_indices.reserve(alive.size());
    for (const engine::ecs::Entity entity : alive) {
        alive_indices.insert(entity.index);
    }
    expansion_.retain([&alive_indices](std::uint32_t index) { return alive_indices.contains(index); });

    const std::string_view prefix = game_ != nullptr
            ? std::string_view("Live game")
            : (edit_label_.empty() ? std::string_view("Scene") : std::string_view(edit_label_));
    const std::string star = (is_dirty_ && game_ == nullptr) ? "*" : "";
    view_model_->statusText = std::format("{}{}  ({} entities)", prefix, star, alive.size());

    std::vector<engine::ecs::Entity> roots;
    roots.reserve(alive.size());
    for (const engine::ecs::Entity entity : alive) {
        const auto* parent = world.try_get<engine::Parent>(entity);
        if (parent == nullptr || !world.valid(parent->entity)) {
            roots.push_back(entity);
        }
    }

    const EntityTreeSource source{.world = &world, .alive = &alive};
    std::vector<std::shared_ptr<HierarchyRowViewModel>> visible;
    std::unordered_map<std::uint32_t, std::shared_ptr<HierarchyRowViewModel>> kept;
    (void) engine::ui::flatten_tree(roots, expansion_, source,
            [&](engine::ecs::Entity entity, const engine::ui::TreeRowInfo& info) {
                std::shared_ptr<HierarchyRowViewModel> slot;
                if (const auto it = rows_.find(entity.index); it != rows_.end()) {
                    slot = it->second;
                } else {
                    slot = std::make_shared<HierarchyRowViewModel>(*this);
                }
                const bool is_selected = entity == selected_;
                const bool is_drop = drop_entity_ && entity == *drop_entity_;
                const bool is_renaming = entity == renaming_;
                slot->show(entity, format_row_label(world, entity), info, is_selected, is_drop, is_renaming);
                visible.push_back(slot);
                kept.emplace(entity.index, std::move(slot));
            });
    rows_ = std::move(kept);
    view_model_->rows.set(std::move(visible));
}

void HierarchyPanel::clear_selection_if_entity() {
    selected_ = {};
    if (std::holds_alternative<EntitySelection>(selection_->target())) {
        selection_->clear();
    }
}

void HierarchyPanel::note_history() {
    if (on_history_) {
        on_history_();
    }
}

}
