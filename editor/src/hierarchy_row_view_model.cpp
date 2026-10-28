#include "hierarchy_row_view_model.h"

#include "hierarchy_panel.h"

#include <asset_ids.h>

#include <utility>

namespace editor {
namespace {

constexpr char kSelectedFill[] = "#2f5d3a";
constexpr char kDropFill[] = "#2a4a6a";
constexpr char kPlainFill[] = "#00000000";

}

HierarchyRowViewModel::HierarchyRowViewModel(HierarchyPanel& panel) : panel_(&panel) {
    assets::ui::Hierarchy::Rows::bind(*this);
    select.bind_to<HierarchyRowViewModel, &HierarchyRowViewModel::select_row>(*this);
    toggle.bind_to<HierarchyRowViewModel, &HierarchyRowViewModel::toggle_row, &HierarchyRowViewModel::can_toggle_row>(
            *this);
    commitRename.bind_to<HierarchyRowViewModel, &HierarchyRowViewModel::commit_rename>(*this);
    createChild.bind_to<HierarchyRowViewModel, &HierarchyRowViewModel::create_child>(*this);
    rename.bind_to<HierarchyRowViewModel, &HierarchyRowViewModel::rename_row>(*this);
    remove.bind_to<HierarchyRowViewModel, &HierarchyRowViewModel::remove_row>(*this);
    menuOpen.on_changed([this](const bool& open) {
        if (open) {
            panel_->select(entity_);
        }
    });
    rowDisplay = "flex";
    renameDisplay = "none";
}

void HierarchyRowViewModel::show(engine::ecs::Entity entity, std::string text,
        const engine::ui::TreeRowInfo& tree, bool selected, bool drop, bool renaming) {
    entity_ = entity;
    tree_ = tree;
    label = std::move(text);
    depth = tree.depth;
    expanded = tree.expanded;
    set_fill(selected, drop);
    rowDisplay = std::string(renaming ? "none" : "flex");
    renameDisplay = std::string(renaming ? "flex" : "none");
}

void HierarchyRowViewModel::set_fill(bool selected, bool drop) {
    rowFill = std::string(selected ? kSelectedFill : drop ? kDropFill : kPlainFill);
}

void HierarchyRowViewModel::seed_edit(const std::string& name) {
    editText = name;
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

void HierarchyRowViewModel::commit_rename() {
    panel_->commit_rename();
}

void HierarchyRowViewModel::create_child() {
    panel_->create_entity(entity_);
}

void HierarchyRowViewModel::rename_row() {
    panel_->begin_rename(entity_);
}

void HierarchyRowViewModel::remove_row() {
    panel_->remove_entity(entity_);
}

}
