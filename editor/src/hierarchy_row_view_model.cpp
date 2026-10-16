#include "hierarchy_row_view_model.h"

#include "hierarchy_panel.h"

#include <asset_ids.h>

#include <utility>

namespace editor {
namespace {

constexpr char kSelectedFill[] = "#2f5d3a";
constexpr char kPlainFill[] = "#00000000";

}

HierarchyRowViewModel::HierarchyRowViewModel(HierarchyPanel& panel) : panel_(&panel) {
    assets::ui::Hierarchy::Rows::bind(*this);
    select.bind_to<HierarchyRowViewModel, &HierarchyRowViewModel::select_row>(*this);
    toggle.bind_to<HierarchyRowViewModel, &HierarchyRowViewModel::toggle_row, &HierarchyRowViewModel::can_toggle_row>(
            *this);
}

void HierarchyRowViewModel::show(
        engine::ecs::Entity entity, std::string text, const engine::ui::TreeRowInfo& tree, bool selected) {
    entity_ = entity;
    tree_ = tree;
    label = std::move(text);
    depth = tree.depth;
    expanded = tree.expanded;
    rowFill = std::string(selected ? kSelectedFill : kPlainFill);
}

void HierarchyRowViewModel::select_row() {
    panel_->select(entity_);
}

void HierarchyRowViewModel::toggle_row() {
    panel_->toggle(entity_);
}

bool HierarchyRowViewModel::can_toggle_row() const {
    return tree_.has_children;
}

}
