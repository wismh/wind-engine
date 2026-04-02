#include "inspector_panel.h"

#include <engine/ecs/world.h>

#include <string>
#include <utility>

namespace editor {
namespace {

constexpr char kIdleHint[] = "Play the game to inspect its UI.";
constexpr char kPickOffHint[] = "Tick Pick, then click the game window to select an element.";
constexpr char kPickOnHint[] = "Clicks in the game window select. Untick Pick to play.";

}

InspectorPanel::InspectorPanel() : view_model_(std::make_shared<InspectorViewModel>()) {
    view_model_->hint = std::string(kIdleHint);
}

const std::shared_ptr<InspectorViewModel>& InspectorPanel::view_model() const {
    return view_model_;
}

void InspectorPanel::attach(engine::ecs::World& game) {
    game_ = &game;
    engine::ui::set_inspector_attached(game, true);
    pick_shown_ = game.ctx<engine::ui::UiInspector>().pick_pointer;
    view_model_->pick = pick_shown_;
}

void InspectorPanel::detach() {
    if (game_ != nullptr) {
        engine::ui::set_inspector_attached(*game_, false);
    }
    game_ = nullptr;
    rows_.clear();
    rules_.clear();
    view_model_->rows.set({});
    view_model_->rules.set({});
    view_model_->detail = std::string();
    pick_shown_ = false;
    view_model_->pick = false;
    view_model_->hint = std::string(kIdleHint);
}

bool InspectorPanel::attached() const {
    return game_ != nullptr;
}

void InspectorPanel::refresh() {
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
    show_selection();
}

void InspectorPanel::select(const engine::ui::InspectorTreeRow& row) {
    if (game_ != nullptr) {
        engine::ui::inspector_select(*game_, row.window, row.pick);
    }
}

void InspectorPanel::toggle(const engine::ui::InspectorRowKey& key) {
    if (game_ != nullptr) {
        engine::ui::inspector_toggle(*game_, key);
    }
}

void InspectorPanel::show_rows() {
    std::vector<engine::ui::InspectorTreeRow> tree = engine::ui::inspector_tree(*game_);
    std::vector<std::shared_ptr<InspectorRowViewModel>> visible;
    visible.reserve(tree.size());
    std::unordered_map<engine::ui::InspectorRowKey, std::shared_ptr<InspectorRowViewModel>,
            engine::ui::InspectorRowKeyHash>
            kept;
    kept.reserve(tree.size());
    for (engine::ui::InspectorTreeRow& row : tree) {
        std::shared_ptr<InspectorRowViewModel> slot;
        if (!kept.contains(row.key)) {
            if (const auto it = rows_.find(row.key); it != rows_.end()) {
                slot = it->second;
            }
        }
        if (!slot) {
            slot = std::make_shared<InspectorRowViewModel>(*this);
        }
        const engine::ui::InspectorRowKey key = row.key;
        slot->show(std::move(row));
        visible.push_back(slot);
        kept.emplace(key, std::move(slot));
    }
    rows_ = std::move(kept);
    view_model_->rows.set(std::move(visible));
}

void InspectorPanel::show_selection() {
    const engine::ui::UiInspector& inspector = game_->ctx<engine::ui::UiInspector>();
    const engine::ui::InspectorPick pick = engine::ui::inspector_selection(*game_, inspector.detail_window);
    view_model_->detail = engine::ui::inspector_detail(*game_, pick);

    std::vector<std::string> lines = engine::ui::inspector_rules(*game_, pick);
    rules_.resize(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (!rules_[i]) {
            rules_[i] = std::make_shared<RuleLineViewModel>();
        }
        rules_[i]->line = std::move(lines[i]);
    }
    view_model_->rules.set(rules_);
}

}
