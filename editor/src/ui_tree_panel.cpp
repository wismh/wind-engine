#include "ui_tree_panel.h"

#include <engine/ecs/world.h>

#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace editor {
namespace {

constexpr char kIdleHint[] = "Play the game to inspect its UI.";
constexpr char kPickOffHint[] = "Tick Pick, then click the game window to select an element.";
constexpr char kPickOnHint[] = "Clicks in the game window select. Untick Pick to play.";

}

UiTreePanel::UiTreePanel(EditorSelection& selection)
    : selection_(&selection), view_model_(std::make_shared<UiTreeViewModel>()) {
    view_model_->hint = std::string(kIdleHint);
}

const std::shared_ptr<UiTreeViewModel>& UiTreePanel::view_model() const {
    return view_model_;
}

void UiTreePanel::attach(engine::ecs::World& game) {
    game_ = &game;
    engine::ui::set_inspector_attached(game, true);
    const engine::ui::UiInspector& inspector = game.ctx<engine::ui::UiInspector>();
    pick_shown_ = inspector.pick_pointer;
    view_model_->pick = pick_shown_;
    selections_seen_ = inspector.selections;
}

void UiTreePanel::detach() {
    if (game_ != nullptr) {
        engine::ui::set_inspector_attached(*game_, false);
    }
    game_ = nullptr;
    rows_.clear();
    view_model_->rows.set({});
    pick_shown_ = false;
    selections_seen_ = 0;
    if (std::holds_alternative<UiElementSelection>(selection_->target())) {
        selection_->clear();
    }
    view_model_->pick = false;
    view_model_->hint = std::string(kIdleHint);
}

bool UiTreePanel::attached() const {
    return game_ != nullptr;
}

void UiTreePanel::sync_selection() {
    if (game_ == nullptr) {
        return;
    }
    const engine::ui::UiInspector& inspector = game_->ctx<engine::ui::UiInspector>();
    if (inspector.selections == selections_seen_) {
        return;
    }
    selections_seen_ = inspector.selections;
    selection_->select(UiElementSelection{.window = inspector.detail_window});
}

void UiTreePanel::refresh() {
    if (game_ == nullptr) {
        return;
    }
    engine::ui::UiInspector& inspector = game_->ctx<engine::ui::UiInspector>();
    if (view_model_->pick.get() != pick_shown_) {
        inspector.pick_pointer = view_model_->pick.get();
    }
    pick_shown_ = inspector.pick_pointer;
    view_model_->pick = pick_shown_;
    view_model_->hint = std::string(pick_shown_ ? kPickOnHint : kPickOffHint);
    show_rows();
    sync_selection();
}

void UiTreePanel::select(const engine::ui::InspectorTreeRow& row) {
    if (game_ != nullptr) {
        engine::ui::inspector_select(*game_, row.window, row.pick);
    }
}

void UiTreePanel::toggle(const engine::ui::InspectorRowKey& key) {
    if (game_ != nullptr) {
        engine::ui::inspector_toggle(*game_, key);
    }
}

std::optional<std::size_t> UiTreePanel::navigate(engine::ui::TreeNav nav) {
    if (game_ == nullptr) {
        return std::nullopt;
    }
    const std::vector<std::shared_ptr<UiTreeRowViewModel>>& rows = view_model_->rows.get();
    const engine::WindowId detail = game_->ctx<engine::ui::UiInspector>().detail_window;
    std::vector<engine::ui::TreeRowInfo> infos;
    infos.reserve(rows.size());
    std::size_t current = engine::ui::kNoTreeRow;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const engine::ui::InspectorTreeRow& row = rows[i]->row();
        infos.push_back(row.tree);
        if (row.selected && (current == engine::ui::kNoTreeRow || row.window == detail)) {
            current = i;
        }
    }
    const engine::ui::TreeNavResult result = engine::ui::tree_navigate(infos, current, nav);
    if (result.row == engine::ui::kNoTreeRow) {
        return std::nullopt;
    }
    const engine::ui::InspectorTreeRow& target = rows[result.row]->row();
    if (result.toggle) {
        toggle(target.key);
    }
    if (result.row != current) {
        select(target);
    }
    return result.row;
}

void UiTreePanel::show_rows() {
    std::vector<engine::ui::InspectorTreeRow> tree = engine::ui::inspector_tree(*game_);
    std::vector<std::shared_ptr<UiTreeRowViewModel>> visible;
    visible.reserve(tree.size());
    std::unordered_map<engine::ui::InspectorRowKey, std::shared_ptr<UiTreeRowViewModel>,
            engine::ui::InspectorRowKeyHash>
            kept;
    kept.reserve(tree.size());
    for (engine::ui::InspectorTreeRow& row : tree) {
        std::shared_ptr<UiTreeRowViewModel> slot;
        if (!kept.contains(row.key)) {
            if (const auto it = rows_.find(row.key); it != rows_.end()) {
                slot = it->second;
            }
        }
        if (!slot) {
            slot = std::make_shared<UiTreeRowViewModel>(*this);
        }
        const engine::ui::InspectorRowKey key = row.key;
        slot->show(std::move(row));
        visible.push_back(slot);
        kept.emplace(key, std::move(slot));
    }
    rows_ = std::move(kept);
    view_model_->rows.set(std::move(visible));
}

}
