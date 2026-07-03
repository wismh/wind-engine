#include "explorer_row_view_model.h"

#include "explorer_panel.h"

#include <asset_ids.h>

namespace editor {
namespace {

constexpr char kSelectedFill[] = "#2f5d3a";
constexpr char kPlainFill[] = "#00000000";

}

ExplorerRowViewModel::ExplorerRowViewModel(ExplorerPanel& panel) : panel_(&panel) {
    assets::ui::Explorer::Rows::bind(*this);
    select.bind_to<ExplorerRowViewModel, &ExplorerRowViewModel::select_row>(*this);
    toggle.bind_to<ExplorerRowViewModel, &ExplorerRowViewModel::toggle_row, &ExplorerRowViewModel::can_toggle_row>(
            *this);
}

void ExplorerRowViewModel::show(const ProjectEntry& entry, const engine::ui::TreeRowInfo& tree, bool selected) {
    key_ = entry.key;
    tree_ = tree;
    label = entry.name;
    depth = tree.depth;
    expanded = tree.expanded;
    rowFill = std::string(selected ? kSelectedFill : kPlainFill);
}

const std::string& ExplorerRowViewModel::key() const {
    return key_;
}

const engine::ui::TreeRowInfo& ExplorerRowViewModel::tree() const {
    return tree_;
}

void ExplorerRowViewModel::select_row() {
    panel_->select(key_);
}

void ExplorerRowViewModel::toggle_row() {
    panel_->toggle(key_);
}

bool ExplorerRowViewModel::can_toggle_row() const {
    return tree_.has_children;
}

}
