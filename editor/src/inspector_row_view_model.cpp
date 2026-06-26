#include "inspector_row_view_model.h"

#include "inspector_panel.h"

#include <asset_ids.h>

#include <utility>

namespace editor {
namespace {

constexpr char kSelectedFill[] = "#2f5d3a";
constexpr char kPlainFill[] = "#00000000";

}

InspectorRowViewModel::InspectorRowViewModel(InspectorPanel& panel) : panel_(&panel) {
    assets::ui::Inspector::Rows::bind(*this);
    select.bind_to<InspectorRowViewModel, &InspectorRowViewModel::select_row>(*this);
    toggle.bind_to<InspectorRowViewModel, &InspectorRowViewModel::toggle_row,
            &InspectorRowViewModel::can_toggle_row>(*this);
}

void InspectorRowViewModel::show(engine::ui::InspectorTreeRow row) {
    row_ = std::move(row);
    label = row_.label;
    depth = row_.tree.depth;
    expanded = row_.tree.expanded;
    rowFill = std::string(row_.selected ? kSelectedFill : kPlainFill);
}

const engine::ui::InspectorTreeRow& InspectorRowViewModel::row() const {
    return row_;
}

void InspectorRowViewModel::select_row() {
    panel_->select(row_);
}

void InspectorRowViewModel::toggle_row() {
    panel_->toggle(row_.key);
}

bool InspectorRowViewModel::can_toggle_row() const {
    return row_.tree.has_children;
}

}
