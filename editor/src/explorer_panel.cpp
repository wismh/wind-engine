#include "explorer_panel.h"

#include "command_history.h"

#include <algorithm>
#include <format>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace editor {
namespace {

constexpr char kNoProject[] = "No project open.";
constexpr char kToggleOn[] = "#3574f0";
constexpr char kToggleOff[] = "#3c3f41";

std::string path_text(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

// flatten_tree over a scan: a node is an entry of it.
struct EntrySource {
    [[nodiscard]] const std::string& key(const ProjectEntry* entry) const { return entry->key; }
    [[nodiscard]] bool has_children(const ProjectEntry* entry) const { return !entry->children.empty(); }
    template<typename Visit>
    void for_each_child(const ProjectEntry* entry, Visit&& visit) const {
        for (const ProjectEntry& child : entry->children) {
            visit(&child);
        }
    }
};

const ProjectEntry* find_entry(const std::vector<ProjectEntry>& roots, std::string_view key) {
    const std::vector<ProjectEntry>* level = &roots;
    const ProjectEntry* found = nullptr;
    while (!key.empty()) {
        const std::size_t slash = key.find('/');
        const std::string_view name = key.substr(0, slash);
        found = nullptr;
        for (const ProjectEntry& entry : *level) {
            if (entry.name == name) {
                found = &entry;
                break;
            }
        }
        if (found == nullptr) {
            return nullptr;
        }
        level = &found->children;
        key = slash == std::string_view::npos ? std::string_view{} : key.substr(slash + 1);
    }
    return found;
}

std::filesystem::path key_path(std::string_view key) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(key.data()), key.size()));
}

void split_leaf(std::string_view name, std::string& stem, std::string& extension) {
    const std::size_t dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == 0) {
        stem = std::string(name);
        extension.clear();
        return;
    }
    stem = std::string(name.substr(0, dot));
    extension = std::string(name.substr(dot));
}

}

ExplorerPanel::ExplorerPanel(EditorSelection& selection, CommandHistory& journal)
    : selection_(&selection), journal_(&journal), view_model_(std::make_shared<ExplorerViewModel>()) {
    view_model_->blankNewFolder.bind_to<ExplorerPanel, &ExplorerPanel::blank_new_folder>(*this);
    view_model_->blankNewWorld.bind_to<ExplorerPanel, &ExplorerPanel::blank_new_world>(*this);
    view_model_->blankNewHeader.bind_to<ExplorerPanel, &ExplorerPanel::blank_new_header>(*this);
    view_model_->blankNewSource.bind_to<ExplorerPanel, &ExplorerPanel::blank_new_source>(*this);
    view_model_->blankPaste.bind_to<ExplorerPanel, &ExplorerPanel::blank_paste, &ExplorerPanel::can_paste>(*this);
    view_model_->showTree.bind_to<ExplorerPanel, &ExplorerPanel::show_tree>(*this);
    view_model_->showGrid.bind_to<ExplorerPanel, &ExplorerPanel::show_grid>(*this);
    view_model_->gridUp.bind_to<ExplorerPanel, &ExplorerPanel::grid_up, &ExplorerPanel::can_grid_up>(*this);
    sync_chrome();
}

const std::shared_ptr<ExplorerViewModel>& ExplorerPanel::view_model() const {
    return view_model_;
}

void ExplorerPanel::open(const std::filesystem::path& directory) {
    directory_ = directory;
    expansion_.reset();
    selected_.clear();
    renaming_.clear();
    drop_.clear();
    rename_focus_ = false;
    clipboard_.reset();
    journal_->discard(EditorCommand::Kind::Project);
    notice_.clear();
    relocations_.clear();
    removals_.clear();
    rows_.clear();
    grid_folder_.clear();
    if (view_ == ExplorerView::Grid) {
        scroll_reset_ = true;
    }
    reload(false);
}

void ExplorerPanel::close() {
    directory_.clear();
    scan_ = {};
    expansion_.reset();
    selected_.clear();
    renaming_.clear();
    drop_.clear();
    rename_focus_ = false;
    clipboard_.reset();
    journal_->discard(EditorCommand::Kind::Project);
    notice_.clear();
    relocations_.clear();
    removals_.clear();
    rows_.clear();
    grid_folder_.clear();
    bring_into_view_ = false;
    view_model_->rows.set({});
    view_model_->tiles.set({});
    publish_selection(false);
    sync_chrome();
}

void ExplorerPanel::rescan() {
    reload(true);
}

void ExplorerPanel::reload(bool external) {
    if (directory_.empty()) {
        return;
    }
    scan_ = scan_project(directory_);
    if (external) {
        journal_->validate(EditorCommand::Kind::Project);
    }
    expansion_.retain([this](const std::string& key) { return find_entry(scan_.roots, key) != nullptr; });
    if (find_entry(scan_.roots, selected_) == nullptr) {
        selected_.clear();
    }
    if (!renaming_.empty() && find_entry(scan_.roots, renaming_) == nullptr) {
        renaming_.clear();
        rename_focus_ = false;
    }
    if (clipboard_ && find_entry(scan_.roots, clipboard_->key) == nullptr) {
        clipboard_.reset();
    }
    if (!drop_.empty() && find_entry(scan_.roots, drop_) == nullptr) {
        drop_.clear();
    }
    retain_grid_folder();
    show_rows();
    publish_selection(false);
}

void ExplorerPanel::show_tree() {
    if (view_ == ExplorerView::Tree) {
        return;
    }
    view_ = ExplorerView::Tree;
    bring_into_view_ = false;
    if (!grid_folder_.empty()) {
        expand_ancestors(grid_folder_);
    }
    if (!selected_.empty()) {
        expand_ancestors(selected_);
    }
    show_rows();
}

void ExplorerPanel::show_grid() {
    if (view_ == ExplorerView::Grid) {
        return;
    }
    view_ = ExplorerView::Grid;
    const ProjectEntry* entry = selected_.empty() ? nullptr : find_entry(scan_.roots, selected_);
    if (entry == nullptr) {
        grid_folder_.clear();
    } else if (entry->directory) {
        grid_folder_ = entry->key;
    } else {
        grid_folder_ = project_parent_key(entry->key);
    }
    scroll_reset_ = true;
    bring_into_view_ = !selected_.empty();
    show_rows();
}

bool ExplorerPanel::open_entry(const std::string& key) {
    if (view_ != ExplorerView::Grid || directory_.empty()) {
        return false;
    }
    const ProjectEntry* entry = find_entry(scan_.roots, key);
    if (entry == nullptr || !entry->directory) {
        return false;
    }
    if (grid_folder_ == entry->key) {
        return true;
    }
    grid_folder_ = entry->key;
    scroll_reset_ = true;
    bring_into_view_ = false;
    show_rows();
    return true;
}

void ExplorerPanel::grid_up() {
    if (!can_grid_up()) {
        return;
    }
    grid_folder_ = project_parent_key(grid_folder_);
    scroll_reset_ = true;
    bring_into_view_ = false;
    show_rows();
}

bool ExplorerPanel::can_grid_up() const {
    return view_ == ExplorerView::Grid && !grid_folder_.empty();
}

void ExplorerPanel::set_grid_columns(int columns) {
    grid_columns_ = std::max(1, columns);
}

bool ExplorerPanel::take_grid_scroll_reset() {
    return std::exchange(scroll_reset_, false);
}

void ExplorerPanel::select(const std::string& key) {
    if (find_entry(scan_.roots, key) == nullptr) {
        return;
    }
    if (!renaming_.empty() && renaming_ != key) {
        renaming_.clear();
        rename_focus_ = false;
    }
    selected_ = key;
    show_rows();
    publish_selection(true);
}

void ExplorerPanel::toggle(const std::string& key) {
    const ProjectEntry* entry = find_entry(scan_.roots, key);
    if (entry == nullptr || entry->children.empty()) {
        return;
    }
    expansion_.toggle(key);
    show_rows();
}

std::optional<std::size_t> ExplorerPanel::navigate(engine::ui::TreeNav nav) {
    if (view_ == ExplorerView::Grid) {
        return navigate_grid(nav);
    }
    const std::vector<std::shared_ptr<ExplorerRowViewModel>>& rows = view_model_->rows.get();
    std::vector<engine::ui::TreeRowInfo> infos;
    infos.reserve(rows.size());
    std::size_t current = engine::ui::kNoTreeRow;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        infos.push_back(rows[i]->tree());
        if (rows[i]->key() == selected_) {
            current = i;
        }
    }
    const engine::ui::TreeNavResult result = engine::ui::tree_navigate(infos, current, nav);
    if (result.row == engine::ui::kNoTreeRow) {
        return std::nullopt;
    }
    // Copied: toggle and select replace the rows.
    const std::string target = rows[result.row]->key();
    if (result.toggle) {
        toggle(target);
    }
    if (result.row != current) {
        select(target);
    }
    return result.row;
}

const std::string& ExplorerPanel::selected() const {
    return selected_;
}

void ExplorerPanel::cut() {
    cut_key(selected_);
}

void ExplorerPanel::copy() {
    copy_key(selected_);
}

void ExplorerPanel::paste() {
    paste_at(selected_);
}

void ExplorerPanel::new_folder() {
    new_folder_at(selected_);
}

void ExplorerPanel::new_world() {
    new_world_at(selected_);
}

void ExplorerPanel::new_header() {
    new_header_at(selected_);
}

void ExplorerPanel::new_source() {
    new_source_at(selected_);
}

void ExplorerPanel::cut_key(const std::string& key) {
    remember(key, true);
}

void ExplorerPanel::copy_key(const std::string& key) {
    remember(key, false);
}

void ExplorerPanel::remember(const std::string& key, bool cut) {
    if (directory_.empty() || find_entry(scan_.roots, key) == nullptr) {
        return;
    }
    clipboard_ = Clipboard{.key = key, .cut = cut};
}

void ExplorerPanel::paste_at(const std::string& anchor) {
    if (directory_.empty() || !clipboard_) {
        return;
    }
    const std::string& from = clipboard_->key;
    if (find_entry(scan_.roots, from) == nullptr) {
        clipboard_.reset();
        return;
    }
    const std::string folder = container_of(anchor);
    const std::string leaf = project_leaf_name(from);
    std::string dest = project_join_key(folder, leaf);
    if (clipboard_->cut) {
        if (dest == from) {
            return;
        }
        apply_result(record(ProjectStep::relocate(*this, directory_, from, dest)));
        if (find_entry(scan_.roots, from) == nullptr) {
            clipboard_.reset();
        }
        return;
    }
    if (find_entry(scan_.roots, dest) != nullptr) {
        std::string stem;
        std::string extension;
        split_leaf(leaf, stem, extension);
        const std::filesystem::path parent = folder.empty() ? directory_ : directory_ / key_path(folder);
        const std::string unique = project_unique_name(parent, stem, extension);
        if (unique.empty()) {
            notice_ = "Something already has that name.";
            return;
        }
        dest = project_join_key(folder, unique);
    }
    apply_result(record(ProjectStep::copy_entry(*this, directory_, from, dest)));
}

void ExplorerPanel::new_folder_at(const std::string& anchor) {
    create_at(anchor, ProjectCreateKind::Directory);
}

void ExplorerPanel::new_world_at(const std::string& anchor) {
    create_at(anchor, ProjectCreateKind::World);
}

void ExplorerPanel::new_header_at(const std::string& anchor) {
    create_at(anchor, ProjectCreateKind::Header);
}

void ExplorerPanel::new_source_at(const std::string& anchor) {
    create_at(anchor, ProjectCreateKind::Source);
}

void ExplorerPanel::create_at(const std::string& anchor, ProjectCreateKind kind) {
    if (directory_.empty()) {
        return;
    }
    const std::string folder = container_of(anchor);
    const std::filesystem::path parent = folder.empty() ? directory_ : directory_ / key_path(folder);
    std::string_view stem = "New Folder";
    std::string_view extension;
    if (kind == ProjectCreateKind::World) {
        stem = "New World";
        extension = ".world";
    } else if (kind == ProjectCreateKind::Header) {
        stem = "New Header";
        extension = ".hpp";
    } else if (kind == ProjectCreateKind::Source) {
        stem = "New Source";
        extension = ".cpp";
    }
    const std::string name = project_unique_name(parent, stem, extension);
    if (name.empty()) {
        notice_ = "Couldn't name the new file.";
        return;
    }
    const std::string key = project_join_key(folder, name);
    ProjectEditResult result = record(ProjectStep::create(*this, directory_, key, kind));
    apply_result(std::move(result));
    if (find_entry(scan_.roots, key) != nullptr) {
        begin_rename(key);
    }
}

void ExplorerPanel::begin_rename() {
    begin_rename(selected_);
}

void ExplorerPanel::begin_rename(const std::string& key) {
    const ProjectEntry* entry = find_entry(scan_.roots, key);
    if (entry == nullptr || key == "wind_project.toml") {
        if (key == "wind_project.toml") {
            notice_ = "wind_project.toml stays where it is.";
        }
        return;
    }
    renaming_ = key;
    rename_focus_ = true;
    // A selected file can sit under a collapsed folder. The field has to be on a shown row.
    expand_ancestors(key);
    show_rows();
    const auto it = rows_.find(key);
    if (it == rows_.end()) {
        renaming_.clear();
        rename_focus_ = false;
        return;
    }
    it->second->seed_edit(entry->name);
}

void ExplorerPanel::commit_rename() {
    if (renaming_.empty()) {
        return;
    }
    const std::string from = renaming_;
    const auto it = rows_.find(from);
    if (it == rows_.end()) {
        renaming_.clear();
        rename_focus_ = false;
        return;
    }
    const std::string name = it->second->editText.get();
    if (name == project_leaf_name(from)) {
        cancel_rename();
        return;
    }
    if (!project_name_ok(name)) {
        notice_ = "That name isn't allowed.";
        return;
    }
    const std::string to = project_join_key(project_parent_key(from), name);
    ProjectEditResult result = record(ProjectStep::relocate(*this, directory_, from, to));
    if (!result.ok) {
        notice_ = std::move(result.notice);
        return;
    }
    renaming_.clear();
    rename_focus_ = false;
    apply_result(std::move(result));
}

void ExplorerPanel::cancel_rename() {
    if (renaming_.empty()) {
        return;
    }
    renaming_.clear();
    rename_focus_ = false;
    show_rows();
}

std::optional<std::string> ExplorerPanel::drop_folder(std::string_view from, std::string_view hover) const {
    if (from.empty() || from == "wind_project.toml") {
        return std::nullopt;
    }
    std::string folder;
    if (hover.empty()) {
        // Empty grid space is the open folder. The tree has no such drop.
        if (view_ != ExplorerView::Grid) {
            return std::nullopt;
        }
        folder = grid_folder_;
    } else {
        const ProjectEntry* entry = find_entry(scan_.roots, hover);
        if (entry == nullptr) {
            return std::nullopt;
        }
        folder = entry->directory ? entry->key : project_parent_key(entry->key);
    }
    if (folder == project_parent_key(from)) {
        return std::nullopt;
    }
    const bool into_self = folder == from ||
            (folder.size() > from.size() && folder.starts_with(from) && folder[from.size()] == '/');
    if (into_self) {
        return std::nullopt;
    }
    const std::string dest = project_join_key(folder, project_leaf_name(from));
    if (dest == from || find_entry(scan_.roots, dest) != nullptr) {
        return std::nullopt;
    }
    return folder;
}

void ExplorerPanel::set_drop_highlight(const std::string& folder) {
    if (folder == drop_) {
        return;
    }
    drop_ = folder;
    for (auto& [key, row] : rows_) {
        row->set_fill(key == selected_, !drop_.empty() && key == drop_);
    }
}

void ExplorerPanel::remove() {
    remove_key(selected_);
}

void ExplorerPanel::remove_key(const std::string& key) {
    if (directory_.empty() || key.empty()) {
        return;
    }
    if (!renaming_.empty() && (renaming_ == key ||
                (renaming_.size() > key.size() && renaming_.starts_with(key) && renaming_[key.size()] == '/'))) {
        cancel_rename();
    }
    if (clipboard_ && (clipboard_->key == key || (clipboard_->key.size() > key.size() &&
                clipboard_->key.starts_with(key) && clipboard_->key[key.size()] == '/'))) {
        clipboard_.reset();
    }
    apply_result(record(ProjectStep::remove(*this, directory_, key)));
}

void ExplorerPanel::move_into(const std::string& from, const std::string& folder) {
    if (directory_.empty() || find_entry(scan_.roots, from) == nullptr) {
        return;
    }
    const std::string dest = project_join_key(folder, project_leaf_name(from));
    if (dest == from) {
        return;
    }
    apply_result(record(ProjectStep::relocate(*this, directory_, from, dest)));
}

ProjectEditResult ExplorerPanel::record(ProjectEditOutcome outcome) {
    if (outcome.step) {
        journal_->push(std::move(outcome.step));
    }
    return std::move(outcome.result);
}

void ExplorerPanel::project_step_ran(const ProjectEditResult& result) {
    renaming_.clear();
    rename_focus_ = false;
    project_result_ = result;
    apply_result(result);
}

ProjectEditResult ExplorerPanel::undo() {
    project_result_ = {};
    (void) journal_->undo();
    return project_result_;
}

ProjectEditResult ExplorerPanel::redo() {
    project_result_ = {};
    (void) journal_->redo();
    return project_result_;
}

std::string ExplorerPanel::key_for(const engine::ui::ViewModel* owner) const {
    if (owner == nullptr) {
        return {};
    }
    for (const auto& [key, row] : rows_) {
        if (row.get() == owner) {
            return key;
        }
    }
    return {};
}

std::string ExplorerPanel::take_notice() {
    return std::exchange(notice_, {});
}

std::vector<ProjectRelocation> ExplorerPanel::take_relocations() {
    std::vector<ProjectRelocation> moved;
    moved.swap(relocations_);
    return moved;
}

std::vector<std::string> ExplorerPanel::take_removals() {
    std::vector<std::string> removed;
    removed.swap(removals_);
    return removed;
}

void ExplorerPanel::show_rows() {
    if (view_ == ExplorerView::Grid) {
        retain_grid_folder();
    }
    std::vector<std::shared_ptr<ExplorerRowViewModel>> visible;
    std::unordered_map<std::string, std::shared_ptr<ExplorerRowViewModel>> kept;
    const auto take = [&](const ProjectEntry& entry, const engine::ui::TreeRowInfo& info) {
        std::shared_ptr<ExplorerRowViewModel> slot;
        if (const auto it = rows_.find(entry.key); it != rows_.end()) {
            slot = it->second;
        } else {
            slot = std::make_shared<ExplorerRowViewModel>(*this);
        }
        slot->show(entry, info, entry.key == selected_, !drop_.empty() && entry.key == drop_,
                entry.key == renaming_);
        visible.push_back(slot);
        kept.emplace(entry.key, std::move(slot));
    };
    if (view_ == ExplorerView::Grid) {
        const std::vector<ProjectEntry>* level = &scan_.roots;
        if (!grid_folder_.empty()) {
            const ProjectEntry* folder = find_entry(scan_.roots, grid_folder_);
            if (folder == nullptr) {
                grid_folder_.clear();
            } else {
                level = &folder->children;
            }
        }
        visible.reserve(level->size());
        for (const ProjectEntry& entry : *level) {
            engine::ui::TreeRowInfo info;
            info.has_children = !entry.children.empty();
            take(entry, info);
        }
        rows_ = std::move(kept);
        view_model_->rows.set({});
        view_model_->tiles.set(std::move(visible));
    } else {
        std::vector<const ProjectEntry*> roots;
        roots.reserve(scan_.roots.size());
        for (const ProjectEntry& entry : scan_.roots) {
            roots.push_back(&entry);
        }
        (void) engine::ui::flatten_tree(roots, expansion_, EntrySource{},
                [&](const ProjectEntry* entry, const engine::ui::TreeRowInfo& info) { take(*entry, info); });
        rows_ = std::move(kept);
        view_model_->tiles.set({});
        view_model_->rows.set(std::move(visible));
    }
    sync_chrome();
}

void ExplorerPanel::sync_chrome() {
    const bool grid = view_ == ExplorerView::Grid;
    view_model_->treeShown = !grid;
    view_model_->gridShown = grid;
    view_model_->treeOn = std::string(grid ? kToggleOff : kToggleOn);
    view_model_->gridOn = std::string(grid ? kToggleOn : kToggleOff);
    publish_root_text();
}

void ExplorerPanel::publish_root_text() {
    if (directory_.empty()) {
        view_model_->rootText = std::string(kNoProject);
        return;
    }
    std::string root = path_text(directory_);
    if (view_ == ExplorerView::Grid && !grid_folder_.empty()) {
        root += " / ";
        root += grid_folder_;
    }
    if (scan_.truncated) {
        root += std::format("  (the first {} entries)", kMaxProjectEntries);
    }
    view_model_->rootText = std::move(root);
}

void ExplorerPanel::retain_grid_folder() {
    while (!grid_folder_.empty()) {
        const ProjectEntry* entry = find_entry(scan_.roots, grid_folder_);
        if (entry != nullptr && entry->directory) {
            return;
        }
        const std::string parent = project_parent_key(grid_folder_);
        if (parent == grid_folder_) {
            grid_folder_.clear();
            scroll_reset_ = true;
            return;
        }
        grid_folder_ = parent;
        scroll_reset_ = true;
    }
}

std::optional<std::size_t> ExplorerPanel::navigate_grid(engine::ui::TreeNav nav) {
    const std::vector<std::shared_ptr<ExplorerRowViewModel>> tiles = view_model_->tiles.get();
    if (tiles.empty()) {
        return std::nullopt;
    }
    const std::size_t last = tiles.size() - 1;
    std::size_t current = tiles.size();
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        if (tiles[i]->key() == selected_) {
            current = i;
            break;
        }
    }
    const auto columns = static_cast<std::size_t>(std::max(1, grid_columns_));
    std::size_t next = 0;
    if (current >= tiles.size()) {
        switch (nav) {
            case engine::ui::TreeNav::Down:
            case engine::ui::TreeNav::First:
                next = 0;
                break;
            case engine::ui::TreeNav::Up:
            case engine::ui::TreeNav::Last:
                next = last;
                break;
            case engine::ui::TreeNav::Left:
            case engine::ui::TreeNav::Right:
                return std::nullopt;
        }
    } else {
        switch (nav) {
            case engine::ui::TreeNav::Left:
                next = current == 0 ? 0 : current - 1;
                break;
            case engine::ui::TreeNav::Right:
                next = current == last ? last : current + 1;
                break;
            case engine::ui::TreeNav::Up:
                next = current < columns ? current : current - columns;
                break;
            case engine::ui::TreeNav::Down:
                next = current + columns > last ? current : current + columns;
                break;
            case engine::ui::TreeNav::First:
                next = 0;
                break;
            case engine::ui::TreeNav::Last:
                next = last;
                break;
        }
        if (next == current) {
            return current;
        }
    }
    const std::string key = tiles[next]->key();
    select(key);
    bring_into_view_ = true;
    return next;
}

std::string ExplorerPanel::blank_target() const {
    return view_ == ExplorerView::Grid ? grid_folder_ : std::string{};
}

void ExplorerPanel::publish_selection(bool take) {
    if (!take && !std::holds_alternative<AssetSelection>(selection_->target())) {
        return;
    }
    const ProjectEntry* entry = selected_.empty() ? nullptr : find_entry(scan_.roots, selected_);
    if (entry == nullptr) {
        if (std::holds_alternative<AssetSelection>(selection_->target())) {
            selection_->clear();
        }
        return;
    }
    selection_->select(AssetSelection{
            .key = entry->key,
            .path = directory_ / key_path(entry->key),
            .directory = entry->directory,
            .size = entry->size,
            .items = entry->children.size(),
    });
}

void ExplorerPanel::apply_result(const ProjectEditResult& result) {
    if (!result.notice.empty()) {
        notice_ = result.notice;
    }
    relocations_.insert(relocations_.end(), result.moved.begin(), result.moved.end());
    removals_.insert(removals_.end(), result.removed.begin(), result.removed.end());
    if (!result.ok || directory_.empty()) {
        return;
    }
    reload(false);
    if (!result.select.empty() && find_entry(scan_.roots, result.select) != nullptr) {
        expand_ancestors(result.select);
        if (view_ == ExplorerView::Grid) {
            const std::string parent = project_parent_key(result.select);
            if (parent != grid_folder_) {
                grid_folder_ = parent;
                scroll_reset_ = true;
            }
        }
        select(result.select);
    }
}

void ExplorerPanel::expand_ancestors(const std::string& key) {
    std::size_t start = 0;
    while (start < key.size()) {
        const std::size_t slash = key.find('/', start);
        if (slash == std::string::npos) {
            break;
        }
        expansion_.set_expanded(key.substr(0, slash), true);
        start = slash + 1;
    }
}

std::string ExplorerPanel::container_of(const std::string& anchor) const {
    if (anchor.empty()) {
        return {};
    }
    const ProjectEntry* entry = find_entry(scan_.roots, anchor);
    if (entry == nullptr) {
        return {};
    }
    if (entry->directory) {
        return entry->key;
    }
    return project_parent_key(entry->key);
}

}
