#include "explorer_row_view_model.h"

#include "explorer_panel.h"

#include <asset_ids.h>

#include <optional>
#include <string_view>

namespace editor {
namespace {

constexpr char kSelectedFill[] = "#2f5d3a";
constexpr char kDropFill[] = "#2a4a6a";
constexpr char kPlainFill[] = "#00000000";

char ascii_lower(unsigned char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
}

bool equal_ascii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (ascii_lower(static_cast<unsigned char>(a[i])) != ascii_lower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string_view extension_of(std::string_view name) {
    const std::size_t dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == name.size()) {
        return {};
    }
    return name.substr(dot + 1);
}

// A folder, a typed document, or the plain document. CMakeLists.txt shares the .cmake icon.
engine::AssetId icon_for(const ProjectEntry& entry) {
    if (entry.directory) {
        return assets::icons::folder;
    }
    if (equal_ascii(entry.name, "CMakeLists.txt")) {
        return assets::icons::file_cmake;
    }
    const std::string_view ext = extension_of(entry.name);
    if (equal_ascii(ext, "css")) {
        return assets::icons::file_css;
    }
    if (equal_ascii(ext, "world")) {
        return assets::icons::file_world;
    }
    if (equal_ascii(ext, "xml")) {
        return assets::icons::file_xml;
    }
    if (equal_ascii(ext, "png")) {
        return assets::icons::file_png;
    }
    if (equal_ascii(ext, "object")) {
        return assets::icons::file_object;
    }
    if (equal_ascii(ext, "cpp")) {
        return assets::icons::file_cpp;
    }
    if (equal_ascii(ext, "hpp")) {
        return assets::icons::file_hpp;
    }
    if (equal_ascii(ext, "cmake")) {
        return assets::icons::file_cmake;
    }
    return assets::icons::file;
}

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
    icon = icon_for(entry);
    const std::optional<engine::AssetId> made = panel_->thumbnail_for(entry);
    if (made) {
        thumbnail = *made;
    }
    hasThumbnail = made.has_value();
    noThumbnail = !made.has_value();
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
