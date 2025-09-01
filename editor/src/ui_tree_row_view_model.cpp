#include "ui_tree_row_view_model.h"

#include "ui_tree_panel.h"

#include <asset_ids.h>

#include <utility>

namespace editor {
namespace {

constexpr char kSelectedFill[] = "#2f5d3a";
constexpr char kPlainFill[] = "#00000000";

}

UiTreeRowViewModel::UiTreeRowViewModel(UiTreePanel& panel) : panel_(&panel) {
    assets::ui::UiTree::Rows::bind(*this);
    select.bind_to<UiTreeRowViewModel, &UiTreeRowViewModel::select_row>(*this);
    toggle.bind_to<UiTreeRowViewModel, &UiTreeRowViewModel::toggle_row,
            &UiTreeRowViewModel::can_toggle_row>(*this);
}

void UiTreeRowViewModel::show(engine::ui::InspectorTreeRow row) {
    row_ = std::move(row);
    label = row_.label;
    depth = row_.tree.depth;
    expanded = row_.tree.expanded;
    rowFill = std::string(row_.selected ? kSelectedFill : kPlainFill);
}

const engine::ui::InspectorTreeRow& UiTreeRowViewModel::row() const {
    return row_;
}

void UiTreeRowViewModel::select_row() {
    panel_->select(row_);
}

void UiTreeRowViewModel::toggle_row() {
    panel_->toggle(row_.key);
}

bool UiTreeRowViewModel::can_toggle_row() const {
    return row_.tree.has_children;
}

}
