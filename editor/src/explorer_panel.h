#pragma once

#include "command_history.h"
#include "editor_selection.h"
#include "explorer_row_view_model.h"
#include "explorer_view_model.h"
#include "project_edit.h"
#include "project_scan.h"

#include <engine/ui/tree.h>
#include <engine/ui/view_model.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace editor {

// The Project tab: the open project's files and folders as a tree (`engine/ui/tree.h`). Selecting one makes it
// the editor's selection, which the Inspector tab shows. The tree is a scan of the disk, taken when a project
// opens, from File -> Refresh, and when the assets watch settles. Folders start collapsed.
//
// Creates, copies, and moves go through `ProjectHistory`, so each one can be undone. Cut and copy only remember a
// key; paste is the edit. Holds `this` in its rows, so it never moves.
class ExplorerPanel {
public:
    explicit ExplorerPanel(EditorSelection& selection);

    ExplorerPanel(const ExplorerPanel&) = delete;
    ExplorerPanel& operator=(const ExplorerPanel&) = delete;

    [[nodiscard]] const std::shared_ptr<ExplorerViewModel>& view_model() const;

    // Scans `directory` (the project directory) and shows it, everything collapsed and nothing selected.
    void open(const std::filesystem::path& directory);
    // No project: no rows, and the editor's selection is cleared when it is a file or folder.
    void close();
    // Refresh from outside this panel (File -> Refresh, the assets watch). Expanded folders and the selection stay
    // where they still exist. A history step whose files or sidecar GUIDs no longer match is dropped.
    void rescan();

    [[nodiscard]] const std::filesystem::path& directory() const { return directory_; }

    // By ProjectEntry::key, here and as the editor's selection. An unknown key selects nothing.
    void select(const std::string& key);
    void toggle(const std::string& key);
    // A tree key on the shown rows from the selected row. Returns the row to keep in view.
    std::optional<std::size_t> navigate(engine::ui::TreeNav nav);

    // The selected key, or empty.
    [[nodiscard]] const std::string& selected() const;

    // Clipboard and new files use the selection: a folder, the parent of a file, or the project root.
    void cut();
    void copy();
    void paste();
    void new_folder();
    void new_world();
    void new_header();
    void new_source();
    // The same actions for one row (its context menu). `key` empty is the project root.
    void cut_key(const std::string& key);
    void copy_key(const std::string& key);
    void paste_at(const std::string& anchor);
    void new_folder_at(const std::string& anchor);
    void new_world_at(const std::string& anchor);
    void new_header_at(const std::string& anchor);
    void new_source_at(const std::string& anchor);
    // Deletes the selection, or `key`. Undo puts the file and its `.meta` back.
    void remove();
    void remove_key(const std::string& key);

    [[nodiscard]] bool can_paste() const { return clipboard_.has_value(); }

    // Inline rename of the selection, or of `key`. Enter commits, Escape cancels.
    void begin_rename();
    void begin_rename(const std::string& key);
    void commit_rename();
    void cancel_rename();
    [[nodiscard]] const std::string& renaming() const { return renaming_; }
    // True until the rename field has been focused. The document may not show it until the next frame.
    [[nodiscard]] bool rename_focus_pending() const { return rename_focus_; }
    void rename_focused() { rename_focus_ = false; }

    // The folder a drag of `from` onto `hover` would move into, or nullopt when the drop does nothing.
    // An empty string is the project root.
    [[nodiscard]] std::optional<std::string> drop_folder(std::string_view from, std::string_view hover) const;
    void set_drop_highlight(const std::string& folder);
    // Moves `from` into `folder` (empty is the project root).
    void move_into(const std::string& from, const std::string& folder);

    [[nodiscard]] ProjectEditResult undo();
    [[nodiscard]] ProjectEditResult redo();

    // File steps are also pushed on `journal`, so Undo reverses a file step or an entity step, whichever was last.
    // The panel's own undo stack still holds the file bytes; the journal only remembers the order.
    void attach_journal(CommandHistory& journal);
    void discard_project_redo();
    [[nodiscard]] bool undo_project_step();
    [[nodiscard]] bool redo_project_step();

    // The row view-model `owner` shows, or empty.
    [[nodiscard]] std::string key_for(const engine::ui::ViewModel* owner) const;

    // A status line from the last edit, then cleared.
    [[nodiscard]] std::string take_notice();
    // Paths the last edits moved, then cleared. Undo reports them reversed.
    [[nodiscard]] std::vector<ProjectRelocation> take_relocations();
    // Keys the last edits deleted, then cleared.
    [[nodiscard]] std::vector<std::string> take_removals();

private:
    void reload(bool external);
    void show_rows();
    // Writes `selected_` as the editor's selection: always when `take` (a click or a tree key), otherwise only
    // when the editor's selection is already a file or folder. A gone entry clears a file or folder selection.
    void publish_selection(bool take);
    void apply_result(const ProjectEditResult& result);
    void expand_ancestors(const std::string& key);
    [[nodiscard]] std::string container_of(const std::string& anchor) const;
    void create_at(const std::string& anchor, ProjectCreateKind kind);
    void remember(const std::string& key, bool cut);
    void blank_new_folder() { new_folder_at({}); }
    void blank_new_world() { new_world_at({}); }
    void blank_new_header() { new_header_at({}); }
    void blank_new_source() { new_source_at({}); }
    void blank_paste() { paste_at({}); }

    struct Clipboard {
        std::string key;
        bool cut = false;
    };

    EditorSelection* selection_;
    std::shared_ptr<ExplorerViewModel> view_model_;
    std::filesystem::path directory_;
    ProjectScan scan_;
    engine::ui::TreeExpansion<std::string> expansion_{false};
    std::string selected_;
    std::string renaming_;
    std::string drop_;
    bool rename_focus_ = false;
    std::optional<Clipboard> clipboard_;
    ProjectHistory history_;
    CommandHistory* journal_ = nullptr;
    ProjectEditResult project_result_;
    std::string notice_;
    std::vector<ProjectRelocation> relocations_;
    std::vector<std::string> removals_;
    // Row view-models by key, so a row keeps its element (hover, scroll) while the tree changes around it.
    std::unordered_map<std::string, std::shared_ptr<ExplorerRowViewModel>> rows_;
};

}
