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

// Tree is the flattened project. Grid is the children of one folder, wrapped by the document.
enum class ExplorerView {
    Tree,
    Grid,
};

// The Project tab: the open project's files and folders (`engine/ui/tree.h` in the tree). Selecting one makes it
// the editor's selection, which the Inspector tab shows. The list is a scan of the disk, taken when a project
// opens, from File -> Refresh, and when the assets watch settles. Folders start collapsed. The view mode lasts for
// the editor session. Opening a project keeps the mode and starts the grid at the project root.
//
// Creates, copies, moves, and deletes are `ProjectStep`s on the editor's `CommandHistory`, so each one can be undone.
// Cut and copy only remember a key; paste is the edit. Holds `this` in its rows, so it never moves.
class ExplorerPanel final : public ProjectStepListener {
public:
    ExplorerPanel(EditorSelection& selection, CommandHistory& journal);

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
    [[nodiscard]] ExplorerView view() const { return view_; }
    // Empty is the project root. Meaningful while the grid is showing.
    [[nodiscard]] const std::string& grid_folder() const { return grid_folder_; }

    // Tree keeps the current folder. Grid opens the selected folder, the parent of the selected file, or the root.
    void show_tree();
    void show_grid();
    // Enters `key` when it is a folder and the grid is showing. A file, and the tree, do nothing.
    bool open_entry(const std::string& key);
    void grid_up();
    [[nodiscard]] bool can_grid_up() const;
    // How many tiles share a line. Keyboard only; the document wraps on its own. At least 1.
    void set_grid_columns(int columns);

    // The grid scroller goes to the top once, after the open folder changes.
    [[nodiscard]] bool take_grid_scroll_reset();
    // A grid key changed the selection and the tile should be brought into view once it has a layout rect.
    [[nodiscard]] bool grid_tile_scroll_pending() const { return bring_into_view_; }
    void finish_grid_tile_scroll() { bring_into_view_ = false; }

    // By ProjectEntry::key, here and as the editor's selection. An unknown key selects nothing.
    void select(const std::string& key);
    void toggle(const std::string& key);
    // A tree key on the shown rows, or a grid step of `grid_columns_` tiles. Returns the row to keep in view.
    // The grid does not use that index: its scroller follows the tile's layout rect.
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

    // A step of this panel ran on the journal (from `undo` here or from the editor's Undo): show what it did.
    void project_step_ran(const ProjectEditResult& result) override;

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
    // Puts the step of a started edit on the journal and hands back what the edit did.
    [[nodiscard]] ProjectEditResult record(ProjectEditOutcome outcome);
    void expand_ancestors(const std::string& key);
    [[nodiscard]] std::string container_of(const std::string& anchor) const;
    void create_at(const std::string& anchor, ProjectCreateKind kind);
    void remember(const std::string& key, bool cut);
    void toggle_view();
    void sync_chrome();
    void publish_root_text();
    // Walks to a folder that still exists. Empty is the project root.
    void retain_grid_folder();
    std::optional<std::size_t> navigate_grid(engine::ui::TreeNav nav);
    // The tree's empty space is the project root. The grid's is the open folder.
    [[nodiscard]] std::string blank_target() const;
    void blank_new_folder() { new_folder_at(blank_target()); }
    void blank_new_world() { new_world_at(blank_target()); }
    void blank_new_header() { new_header_at(blank_target()); }
    void blank_new_source() { new_source_at(blank_target()); }
    void blank_paste() { paste_at(blank_target()); }

    struct Clipboard {
        std::string key;
        bool cut = false;
    };

    EditorSelection* selection_;
    CommandHistory* journal_;
    std::shared_ptr<ExplorerViewModel> view_model_;
    std::filesystem::path directory_;
    ProjectScan scan_;
    ExplorerView view_ = ExplorerView::Tree;
    // The grid's open folder. Empty is the project root. Kept while the tree is showing, until the next show_grid.
    std::string grid_folder_;
    int grid_columns_ = 1;
    bool scroll_reset_ = false;
    bool bring_into_view_ = false;
    engine::ui::TreeExpansion<std::string> expansion_{false};
    std::string selected_;
    std::string renaming_;
    std::string drop_;
    bool rename_focus_ = false;
    std::optional<Clipboard> clipboard_;
    ProjectEditResult project_result_;
    std::string notice_;
    std::vector<ProjectRelocation> relocations_;
    std::vector<std::string> removals_;
    // Row view-models by key, so a row keeps its element (hover, scroll) while the tree changes around it.
    std::unordered_map<std::string, std::shared_ptr<ExplorerRowViewModel>> rows_;
};

}
