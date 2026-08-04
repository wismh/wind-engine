#include "explorer_row_view_model.h"

#include "explorer_panel.h"

#include <asset_ids.h>

namespace editor {
namespace {

constexpr char kSelectedFill[] = "#2f5d3a";
constexpr char kDropFill[] = "#2a4a6a";
constexpr char kPlainFill[] = "#00000000";

}

ExplorerRowViewModel::ExplorerRowViewModel(ExplorerPanel& panel) : panel_(&panel) {
    assets::ui::Explorer::Rows::bind(*this);
    select.bind_to<ExplorerRowViewModel, &ExplorerRowViewModel::select_row>(*this);
    toggle.bind_to<ExplorerRowViewModel, &ExplorerRowViewModel::toggle_row, &ExplorerRowViewModel::can_toggle_row>(
            *this);
    commitRename.bind_to<ExplorerRowViewModel, &ExplorerRowViewModel::commit_rename>(*this);
    newFolder.bind_to<ExplorerRowViewModel, &ExplorerRowViewModel::new_folder>(*this);
    newWorld.bind_to<ExplorerRowViewModel, &ExplorerRowViewModel::new_world>(*this);
    newHeader.bind_to<ExplorerRowViewModel, &ExplorerRowViewModel::new_header>(*this);
    newSource.bind_to<ExplorerRowViewModel, &ExplorerRowViewModel::new_source>(*this);
    cut.bind_to<ExplorerRowViewModel, &ExplorerRowViewModel::cut_row>(*this);
    copy.bind_to<ExplorerRowViewModel, &ExplorerRowViewModel::copy_row>(*this);
    paste.bind_to<ExplorerRowViewModel, &ExplorerRowViewModel::paste_row, &ExplorerRowViewModel::can_paste_row>(*this);
    rename.bind_to<ExplorerRowViewModel, &ExplorerRowViewModel::rename_row>(*this);
    remove.bind_to<ExplorerRowViewModel, &ExplorerRowViewModel::remove_row>(*this);
    menuOpen.on_changed([this](const bool& open) {
        if (open) {
            panel_->select(key_);
        }
    });
    rowDisplay = "flex";
    renameDisplay = "none";
}

void ExplorerRowViewModel::show(const ProjectEntry& entry, const engine::ui::TreeRowInfo& tree, bool selected,
        bool drop, bool renaming) {
    key_ = entry.key;
    tree_ = tree;
    label = entry.name;
    icon = entry.directory ? assets::textures::folder : assets::textures::file;
    depth = tree.depth;
    expanded = tree.expanded;
    set_fill(selected, drop);
    rowDisplay = std::string(renaming ? "none" : "flex");
    renameDisplay = std::string(renaming ? "flex" : "none");
}

void ExplorerRowViewModel::set_fill(bool selected, bool drop) {
    rowFill = std::string(selected ? kSelectedFill : drop ? kDropFill : kPlainFill);
}

void ExplorerRowViewModel::seed_edit(const std::string& name) {
    editText = name;
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

void ExplorerRowViewModel::commit_rename() {
    panel_->commit_rename();
}

void ExplorerRowViewModel::new_folder() {
    panel_->new_folder_at(key_);
}

void ExplorerRowViewModel::new_world() {
    panel_->new_world_at(key_);
}

void ExplorerRowViewModel::new_header() {
    panel_->new_header_at(key_);
}

void ExplorerRowViewModel::new_source() {
    panel_->new_source_at(key_);
}

void ExplorerRowViewModel::cut_row() {
    panel_->cut_key(key_);
}

void ExplorerRowViewModel::copy_row() {
    panel_->copy_key(key_);
}

void ExplorerRowViewModel::paste_row() {
    panel_->paste_at(key_);
}

bool ExplorerRowViewModel::can_paste_row() const {
    return panel_->can_paste();
}

void ExplorerRowViewModel::rename_row() {
    panel_->begin_rename(key_);
}

void ExplorerRowViewModel::remove_row() {
    panel_->remove_key(key_);
}

}
