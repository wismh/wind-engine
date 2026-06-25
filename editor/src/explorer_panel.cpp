#include "explorer_panel.h"

#include "command_history.h"

#include <format>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace editor {
namespace {

class ProjectStepCommand final : public EditorCommand {
public:
    explicit ProjectStepCommand(ExplorerPanel& panel) : panel_(&panel) {}

    bool undo() override { return panel_->undo_project_step(); }
    bool redo() override { return panel_->redo_project_step(); }
    [[nodiscard]] Kind kind() const override { return Kind::Project; }

private:
    ExplorerPanel* panel_;
};

}
namespace {

constexpr char kNoProject[] = "No project open.";

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

ExplorerPanel::ExplorerPanel(EditorSelection& selection)
    : selection_(&selection), view_model_(std::make_shared<ExplorerViewModel>()) {
    view_model_->rootText = std::string(kNoProject);
    view_model_->blankNewFolder.bind_to<ExplorerPanel, &ExplorerPanel::blank_new_folder>(*this);
    view_model_->blankNewWorld.bind_to<ExplorerPanel, &ExplorerPanel::blank_new_world>(*this);
    view_model_->blankNewHeader.bind_to<ExplorerPanel, &ExplorerPanel::blank_new_header>(*this);
    view_model_->blankNewSource.bind_to<ExplorerPanel, &ExplorerPanel::blank_new_source>(*this);
    view_model_->blankPaste.bind_to<ExplorerPanel, &ExplorerPanel::blank_paste, &ExplorerPanel::can_paste>(*this);
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
    history_.clear();
    notice_.clear();
    relocations_.clear();
    removals_.clear();
    rows_.clear();
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
    history_.clear();
    notice_.clear();
    relocations_.clear();
    removals_.clear();
    rows_.clear();
    view_model_->rows.set({});
    view_model_->rootText = std::string(kNoProject);
    publish_selection(false);
}

void ExplorerPanel::rescan() {
    reload(true);
}

void ExplorerPanel::reload(bool external) {
    if (directory_.empty()) {
        return;
    }
    scan_ = scan_project(directory_);
    if (external && !history_.matches(directory_)) {
        history_.clear();
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
    std::string root = path_text(directory_);
    if (scan_.truncated) {
        root += std::format("  (the first {} entries)", kMaxProjectEntries);
    }
    view_model_->rootText = std::move(root);
    show_rows();
    publish_selection(false);
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
        apply_result(history_.relocate(directory_, from, dest));
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
    apply_result(history_.copy_entry(directory_, from, dest));
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
    ProjectEditResult result = history_.create(directory_, key, kind);
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
    if (const auto it = rows_.find(key); it != rows_.end()) {
        it->second->seed_edit(entry->name);
    }
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
    ProjectEditResult result = history_.relocate(directory_, from, to);
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
    if (from.empty() || hover.empty() || from == "wind_project.toml") {
        return std::nullopt;
    }
    const ProjectEntry* entry = find_entry(scan_.roots, hover);
    if (entry == nullptr) {
        return std::nullopt;
    }
    const std::string folder = entry->directory ? entry->key : project_parent_key(entry->key);
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
    apply_result(history_.remove(directory_, key));
}

void ExplorerPanel::move_into(const std::string& from, const std::string& folder) {
    if (directory_.empty() || find_entry(scan_.roots, from) == nullptr) {
        return;
    }
    const std::string dest = project_join_key(folder, project_leaf_name(from));
    if (dest == from) {
        return;
    }
    apply_result(history_.relocate(directory_, from, dest));
}

void ExplorerPanel::attach_journal(CommandHistory& journal) {
    journal_ = &journal;
    history_.set_hooks(
            [this] {
                if (journal_ != nullptr) {
                    journal_->push(std::make_unique<ProjectStepCommand>(*this));
                }
            },
            [this] {
                if (journal_ != nullptr) {
                    journal_->discard(EditorCommand::Kind::Project);
                }
            });
}

void ExplorerPanel::discard_project_redo() {
    history_.clear_redo();
}

bool ExplorerPanel::undo_project_step() {
    if (directory_.empty()) {
        return false;
    }
    renaming_.clear();
    rename_focus_ = false;
    project_result_ = history_.undo(directory_);
    apply_result(project_result_);
    return project_result_.ok;
}

bool ExplorerPanel::redo_project_step() {
    if (directory_.empty()) {
        return false;
    }
    renaming_.clear();
    rename_focus_ = false;
    project_result_ = history_.redo(directory_);
    apply_result(project_result_);
    return project_result_.ok;
}

ProjectEditResult ExplorerPanel::undo() {
    if (journal_ != nullptr) {
        project_result_ = {};
        if (!journal_->undo()) {
            return {};
        }
        return project_result_;
    }
    if (!undo_project_step()) {
        return project_result_;
    }
    return project_result_;
}

ProjectEditResult ExplorerPanel::redo() {
    if (journal_ != nullptr) {
        project_result_ = {};
        if (!journal_->redo()) {
            return {};
        }
        return project_result_;
    }
    if (!redo_project_step()) {
        return project_result_;
    }
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
    std::vector<const ProjectEntry*> roots;
    roots.reserve(scan_.roots.size());
    for (const ProjectEntry& entry : scan_.roots) {
        roots.push_back(&entry);
    }
    std::vector<std::shared_ptr<ExplorerRowViewModel>> visible;
    std::unordered_map<std::string, std::shared_ptr<ExplorerRowViewModel>> kept;
    (void) engine::ui::flatten_tree(roots, expansion_, EntrySource{},
            [&](const ProjectEntry* entry, const engine::ui::TreeRowInfo& info) {
                std::shared_ptr<ExplorerRowViewModel> slot;
                if (const auto it = rows_.find(entry->key); it != rows_.end()) {
                    slot = it->second;
                } else {
                    slot = std::make_shared<ExplorerRowViewModel>(*this);
                }
                slot->show(*entry, info, entry->key == selected_, !drop_.empty() && entry->key == drop_,
                        entry->key == renaming_);
                visible.push_back(slot);
                kept.emplace(entry->key, std::move(slot));
            });
    rows_ = std::move(kept);
    view_model_->rows.set(std::move(visible));
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
