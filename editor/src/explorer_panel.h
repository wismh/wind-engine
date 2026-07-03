#pragma once

#include "explorer_row_view_model.h"
#include "explorer_view_model.h"
#include "project_scan.h"

#include <engine/ui/tree.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace editor {

// The Project tab: the open project's files and folders as a tree (`engine/ui/tree.h`), and what the selected
// one is. The tree is a scan of the disk, taken when a project opens and again on Refresh; between scans it
// does not change. Folders start collapsed. Holds `this` in its rows and its Refresh command, so it never moves.
class ExplorerPanel {
public:
    ExplorerPanel();

    ExplorerPanel(const ExplorerPanel&) = delete;
    ExplorerPanel& operator=(const ExplorerPanel&) = delete;

    [[nodiscard]] const std::shared_ptr<ExplorerViewModel>& view_model() const;

    // Scans `directory` (the project directory) and shows it, everything collapsed and nothing selected.
    void open(const std::filesystem::path& directory);
    // No project: no rows.
    void close();
    // Refresh: scans the open project again. Expanded folders and the selection stay where they still exist.
    void rescan();
    [[nodiscard]] bool can_rescan() const;

    // By ProjectEntry::key. An unknown key selects nothing.
    void select(const std::string& key);
    void toggle(const std::string& key);
    // A tree key on the shown rows from the selected row. Returns the row to keep in view.
    std::optional<std::size_t> navigate(engine::ui::TreeNav nav);

    // The selected key, or empty.
    [[nodiscard]] const std::string& selected() const;

private:
    void show_rows();
    void show_selection();

    std::shared_ptr<ExplorerViewModel> view_model_;
    std::filesystem::path directory_;
    ProjectScan scan_;
    engine::ui::TreeExpansion<std::string> expansion_{false};
    std::string selected_;
    // Row view-models by key, so a row keeps its element (hover, scroll) while the tree changes around it.
    std::unordered_map<std::string, std::shared_ptr<ExplorerRowViewModel>> rows_;
};

}
