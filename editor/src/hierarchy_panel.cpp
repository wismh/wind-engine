#include "hierarchy_panel.h"

#include "entity_inspection.h"

#include <engine/ecs/camera.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_io.h>

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

}

HierarchyPanel::HierarchyPanel(EditorSelection& selection)
    : selection_(&selection), view_model_(std::make_shared<HierarchyViewModel>()) {
    view_model_->statusText = std::string(kIdleHint);
}

const std::shared_ptr<HierarchyViewModel>& HierarchyPanel::view_model() const {
    return view_model_;
}

void HierarchyPanel::attach(engine::ecs::World& game) {
    game_ = &game;
    expansion_.reset();
    rows_.clear();
    clear_selection_if_entity();
}

void HierarchyPanel::detach() {
    game_ = nullptr;
    expansion_.reset();
    rows_.clear();
    view_model_->rows.set({});
    clear_selection_if_entity();
    if (edit_world_ == nullptr) {
        view_model_->statusText = std::string(kIdleHint);
    }
}

bool HierarchyPanel::attached() const {
    return game_ != nullptr;
}

void HierarchyPanel::set_edit_world(engine::ecs::World* edit_world, std::string_view world_label) {
    const bool world_changed = edit_world != edit_world_ || edit_label_ != world_label;
    edit_world_ = edit_world;
    edit_label_ = std::string(world_label);
    if (game_ == nullptr) {
        if (world_changed || (selected_ != engine::ecs::Entity{} &&
                (edit_world_ == nullptr || !edit_world_->valid(selected_)))) {
            clear_selection_if_entity();
        }
        if (world_changed) {
            expansion_.reset();
            rows_.clear();
            if (edit_world_ == nullptr) {
                view_model_->rows.set({});
                view_model_->statusText = std::string(kIdleHint);
            }
        }
    }
}

void HierarchyPanel::refresh() {
    const engine::ecs::World* world = active_world();
    if (world == nullptr) {
        clear_selection_if_entity();
        rows_.clear();
        view_model_->rows.set({});
        view_model_->statusText = std::string(kIdleHint);
        return;
    }

    if (selected_ != engine::ecs::Entity{} && !world->valid(selected_)) {
        clear_selection_if_entity();
    } else if (const auto* ent_sel = std::get_if<EntitySelection>(&selection_->target());
            ent_sel != nullptr && !world->valid(ent_sel->entity)) {
        selection_->clear();
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
    // Copied: toggle and select replace the rows.
    const engine::ecs::Entity target = rows[result.row]->entity();
    if (result.toggle) {
        toggle(target);
    }
    if (result.row != current) {
        select(target);
    }
    return result.row;
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
    view_model_->statusText = std::format("{}  ({} entities)", prefix, alive.size());

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
                slot->show(entity, format_row_label(world, entity), info, entity == selected_);
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

}
