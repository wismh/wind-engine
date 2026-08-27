#include <gtest/gtest.h>

#include "editor_selection.h"
#include "explorer_panel.h"
#include "project_scan.h"

#include <asset_ids.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

namespace {

// A project directory per test:
//   assets/ui/menu.xml (+ .meta), assets/Images/, src/game.cpp, CMakeLists.txt, wind_project.toml,
//   and the hidden .git/, build/, build-editor/, out/, .gitignore.
class ProjectDir {
public:
    ProjectDir()
        : root_(std::filesystem::temp_directory_path() / "wind_explorer_panel_test" /
                  ::testing::UnitTest::GetInstance()->current_test_info()->name()) {
        std::filesystem::remove_all(root_);
        for (const char* dir : {"assets/ui", "assets/Images", "src", ".git", "build/x", "build-editor", "out"}) {
            std::filesystem::create_directories(root_ / dir);
        }
        write("assets/ui/menu.xml", "<Canvas/>");
        write("assets/ui/menu.xml.meta", "guid = \"x\"");
        write("src/game.cpp", std::string(2048, 'x'));
        write("CMakeLists.txt", "");
        write("wind_project.toml", "");
        write(".gitignore", "");
    }
    ~ProjectDir() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }
    ProjectDir(const ProjectDir&) = delete;
    ProjectDir& operator=(const ProjectDir&) = delete;

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

    void write(const std::string& relative, const std::string& text) const {
        std::ofstream(root_ / relative, std::ios::binary) << text;
    }

private:
    std::filesystem::path root_;
};

std::vector<std::string> names(const std::vector<editor::ProjectEntry>& entries) {
    std::vector<std::string> out;
    for (const editor::ProjectEntry& entry : entries) {
        out.push_back(entry.name);
    }
    return out;
}

// Each row's label, indented two spaces per depth.
std::vector<std::string> tile_names(const editor::ExplorerViewModel& vm) {
    std::vector<std::string> out;
    for (const auto& tile : vm.tiles.get()) {
        out.push_back(tile->label.get());
    }
    return out;
}

std::vector<std::string> labels(const editor::ExplorerViewModel& vm) {
    std::vector<std::string> out;
    for (const auto& row : vm.rows.get()) {
        out.push_back(std::string(static_cast<std::size_t>(row->depth.get()) * 2, ' ') + row->label.get());
    }
    return out;
}

std::shared_ptr<editor::ExplorerRowViewModel> row_of(const editor::ExplorerViewModel& vm, const std::string& key) {
    for (const auto& row : vm.rows.get()) {
        if (row->key() == key) {
            return row;
        }
    }
    ADD_FAILURE() << "no row " << key;
    return nullptr;
}

editor::AssetSelection asset_of(const editor::EditorSelection& selection) {
    const auto* asset = std::get_if<editor::AssetSelection>(&selection.target());
    if (asset == nullptr) {
        ADD_FAILURE() << "the selection is not a file or folder";
        return {};
    }
    return *asset;
}

}

TEST(ProjectScan, HidesDotNamesBuildTreesAndMetaFiles) {
    EXPECT_TRUE(editor::hidden_in_project(".git", true));
    EXPECT_TRUE(editor::hidden_in_project(".gitignore", false));
    EXPECT_TRUE(editor::hidden_in_project("build", true));
    EXPECT_TRUE(editor::hidden_in_project("build-editor", true));
    EXPECT_TRUE(editor::hidden_in_project("cmake-build-debug", true));
    EXPECT_TRUE(editor::hidden_in_project("out", true));
    EXPECT_TRUE(editor::hidden_in_project("menu.xml.meta", false));
    EXPECT_FALSE(editor::hidden_in_project("build", false)) << "only a build folder is a build tree";
    EXPECT_FALSE(editor::hidden_in_project("builder", true));
    EXPECT_FALSE(editor::hidden_in_project("assets", true));
    EXPECT_FALSE(editor::hidden_in_project("menu.xml", false));
}

TEST(ProjectScan, FoldersFirstThenFilesByNameIgnoringCase) {
    const ProjectDir dir;
    const editor::ProjectScan scan = editor::scan_project(dir.root());
    EXPECT_FALSE(scan.truncated);
    EXPECT_EQ(names(scan.roots), (std::vector<std::string>{"assets", "src", "CMakeLists.txt", "wind_project.toml"}));
    const editor::ProjectEntry& assets = scan.roots[0];
    EXPECT_TRUE(assets.directory);
    EXPECT_EQ(assets.key, "assets");
    ASSERT_EQ(names(assets.children), (std::vector<std::string>{"Images", "ui"}));
    const editor::ProjectEntry& ui = assets.children[1];
    ASSERT_EQ(ui.children.size(), 1u) << "the .meta sidecar is hidden";
    const editor::ProjectEntry& menu = ui.children[0];
    EXPECT_EQ(menu.key, "assets/ui/menu.xml");
    EXPECT_FALSE(menu.directory);
    EXPECT_EQ(menu.size, 9u);
    // assets, Images, ui, menu.xml, src, game.cpp, CMakeLists.txt, wind_project.toml.
    EXPECT_EQ(scan.count, 8u);
}

TEST(ProjectScan, AMissingDirectoryIsEmpty) {
    const editor::ProjectScan scan = editor::scan_project(std::filesystem::temp_directory_path() / "wind_no_such_dir");
    EXPECT_TRUE(scan.roots.empty());
    EXPECT_EQ(scan.count, 0u);
}

TEST(ExplorerPanel, NoProjectShowsNothing) {
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::ExplorerPanel panel{selection, history};
    const editor::ExplorerViewModel& vm = *panel.view_model();
    EXPECT_TRUE(vm.rows.get().empty());
    EXPECT_EQ(vm.rootText.get(), "No project open.");
}

TEST(ExplorerPanel, OpenShowsTheCollapsedRootsAndExpandsOnToggle) {
    const ProjectDir dir;
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::ExplorerPanel panel{selection, history};
    panel.open(dir.root());
    editor::ExplorerViewModel& vm = *panel.view_model();
    EXPECT_EQ(labels(vm), (std::vector<std::string>{"assets", "src", "CMakeLists.txt", "wind_project.toml"}));
    EXPECT_TRUE(std::holds_alternative<std::monostate>(selection.target()));
    EXPECT_FALSE(row_of(vm, "assets")->expanded.get());
    EXPECT_FALSE(row_of(vm, "CMakeLists.txt")->toggle.can_execute()) << "a file has no expander";

    row_of(vm, "assets")->toggle.execute();
    EXPECT_EQ(labels(vm), (std::vector<std::string>{"assets", "  Images", "  ui", "src", "CMakeLists.txt",
                                  "wind_project.toml"}));
    EXPECT_TRUE(row_of(vm, "assets")->expanded.get());
    EXPECT_FALSE(row_of(vm, "assets/Images")->toggle.can_execute()) << "an empty folder has no expander";

    row_of(vm, "assets")->toggle.execute();
    EXPECT_EQ(vm.rows.get().size(), 4u);
}

TEST(ExplorerPanel, RowsUseFolderAndFileIcons) {
    const ProjectDir dir;
    dir.write("sheet.css", "");
    dir.write("level.world", "");
    dir.write("sprite.png", "");
    dir.write("photo.PNG", "");
    dir.write("player.object", "");
    dir.write("widget.hpp", "");
    dir.write("rules.cmake", "");
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::ExplorerPanel panel{selection, history};
    panel.open(dir.root());
    editor::ExplorerViewModel& vm = *panel.view_model();
    EXPECT_EQ(row_of(vm, "assets")->icon.get(), assets::icons::folder);
    EXPECT_EQ(row_of(vm, "src")->icon.get(), assets::icons::folder);
    EXPECT_EQ(row_of(vm, "CMakeLists.txt")->icon.get(), assets::icons::file_cmake);
    EXPECT_EQ(row_of(vm, "rules.cmake")->icon.get(), assets::icons::file_cmake);
    EXPECT_EQ(row_of(vm, "wind_project.toml")->icon.get(), assets::icons::file);
    EXPECT_EQ(row_of(vm, "sheet.css")->icon.get(), assets::icons::file_css);
    EXPECT_EQ(row_of(vm, "level.world")->icon.get(), assets::icons::file_world);
    EXPECT_EQ(row_of(vm, "sprite.png")->icon.get(), assets::icons::file_png);
    EXPECT_EQ(row_of(vm, "photo.PNG")->icon.get(), assets::icons::file_png);
    EXPECT_EQ(row_of(vm, "player.object")->icon.get(), assets::icons::file_object);
    EXPECT_EQ(row_of(vm, "widget.hpp")->icon.get(), assets::icons::file_hpp);

    panel.toggle("assets");
    EXPECT_EQ(row_of(vm, "assets/Images")->icon.get(), assets::icons::folder);
    panel.toggle("assets/ui");
    EXPECT_EQ(row_of(vm, "assets/ui/menu.xml")->icon.get(), assets::icons::file_xml);
    panel.toggle("src");
    EXPECT_EQ(row_of(vm, "src/game.cpp")->icon.get(), assets::icons::file_cpp);
}

TEST(ExplorerPanel, SelectBecomesTheEditorSelection) {
    const ProjectDir dir;
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::ExplorerPanel panel{selection, history};
    panel.open(dir.root());
    editor::ExplorerViewModel& vm = *panel.view_model();
    panel.toggle("src");
    row_of(vm, "src/game.cpp")->select.execute();
    EXPECT_EQ(panel.selected(), "src/game.cpp");
    EXPECT_EQ(asset_of(selection),
            (editor::AssetSelection{.key = "src/game.cpp", .path = dir.root() / "src/game.cpp", .size = 2048}));
    EXPECT_NE(row_of(vm, "src/game.cpp")->rowFill.get(), row_of(vm, "src")->rowFill.get());

    row_of(vm, "src")->select.execute();
    EXPECT_EQ(asset_of(selection),
            (editor::AssetSelection{.key = "src", .path = dir.root() / "src", .directory = true, .items = 1}));
    const std::uint64_t revision = selection.revision();
    panel.select("src");
    EXPECT_GT(selection.revision(), revision) << "selecting again is a new selection: the Inspector reads it again";
    panel.select("no/such");
    EXPECT_EQ(panel.selected(), "src") << "an unknown key leaves the selection";
    EXPECT_EQ(asset_of(selection).key, "src");
}

TEST(ExplorerPanel, RowsKeepTheirViewModelWhileTheTreeChanges) {
    const ProjectDir dir;
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::ExplorerPanel panel{selection, history};
    panel.open(dir.root());
    editor::ExplorerViewModel& vm = *panel.view_model();
    const std::shared_ptr<editor::ExplorerRowViewModel> src = row_of(vm, "src");
    panel.toggle("assets");
    EXPECT_EQ(row_of(vm, "src"), src);
}

TEST(ExplorerPanel, TreeKeysMoveExpandAndCollapse) {
    const ProjectDir dir;
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::ExplorerPanel panel{selection, history};
    panel.open(dir.root());
    editor::ExplorerViewModel& vm = *panel.view_model();

    EXPECT_EQ(panel.navigate(engine::ui::TreeNav::Down), 0u);
    EXPECT_EQ(panel.selected(), "assets");
    EXPECT_EQ(panel.navigate(engine::ui::TreeNav::Right), 0u);
    EXPECT_TRUE(row_of(vm, "assets")->expanded.get());
    EXPECT_EQ(panel.navigate(engine::ui::TreeNav::Right), 1u);
    EXPECT_EQ(panel.selected(), "assets/Images");
    EXPECT_EQ(panel.navigate(engine::ui::TreeNav::Left), 0u);
    EXPECT_EQ(panel.selected(), "assets");
    EXPECT_EQ(panel.navigate(engine::ui::TreeNav::Left), 0u);
    EXPECT_FALSE(row_of(vm, "assets")->expanded.get());
    EXPECT_EQ(panel.navigate(engine::ui::TreeNav::Last), 3u);
    EXPECT_EQ(panel.selected(), "wind_project.toml");
}

TEST(ExplorerPanel, RefreshKeepsExpansionAndSelectionThatStillExist) {
    const ProjectDir dir;
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::ExplorerPanel panel{selection, history};
    panel.open(dir.root());
    editor::ExplorerViewModel& vm = *panel.view_model();
    panel.toggle("assets");
    panel.toggle("assets/ui");
    panel.select("assets/ui/menu.xml");

    dir.write("assets/ui/hud.xml", "<Canvas/>");
    panel.rescan();
    EXPECT_EQ(labels(vm), (std::vector<std::string>{"assets", "  Images", "  ui", "    hud.xml", "    menu.xml",
                                  "src", "CMakeLists.txt", "wind_project.toml"}));
    EXPECT_EQ(panel.selected(), "assets/ui/menu.xml");
    EXPECT_EQ(asset_of(selection).key, "assets/ui/menu.xml");

    std::filesystem::remove(dir.root() / "assets/ui/menu.xml");
    panel.rescan();
    EXPECT_TRUE(panel.selected().empty()) << "the selected file is gone";
    EXPECT_TRUE(std::holds_alternative<std::monostate>(selection.target()));
    EXPECT_TRUE(row_of(vm, "assets/ui")->expanded.get());
}

TEST(ExplorerPanel, RefreshLeavesAnotherPanelsSelection) {
    const ProjectDir dir;
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::ExplorerPanel panel{selection, history};
    panel.open(dir.root());
    panel.select("src");
    selection.select(editor::UiElementSelection{});
    const std::uint64_t revision = selection.revision();

    panel.rescan();
    EXPECT_TRUE(std::holds_alternative<editor::UiElementSelection>(selection.target()));
    EXPECT_EQ(selection.revision(), revision);
    EXPECT_EQ(panel.selected(), "src") << "the Project tab keeps its own highlight";

    std::filesystem::remove_all(dir.root() / "src");
    panel.rescan();
    EXPECT_TRUE(std::holds_alternative<editor::UiElementSelection>(selection.target()));
    panel.close();
    EXPECT_TRUE(std::holds_alternative<editor::UiElementSelection>(selection.target()));
}

TEST(ExplorerPanel, OpenAgainStartsCollapsedAndCloseClears) {
    const ProjectDir dir;
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::ExplorerPanel panel{selection, history};
    panel.open(dir.root());
    panel.toggle("assets");
    panel.select("assets");
    panel.open(dir.root());
    editor::ExplorerViewModel& vm = *panel.view_model();
    EXPECT_EQ(vm.rows.get().size(), 4u);
    EXPECT_TRUE(panel.selected().empty());
    EXPECT_TRUE(std::holds_alternative<std::monostate>(selection.target()));

    panel.select("src");
    panel.close();
    EXPECT_TRUE(std::holds_alternative<std::monostate>(selection.target()));
    EXPECT_TRUE(vm.rows.get().empty());
    EXPECT_EQ(vm.rootText.get(), "No project open.");
}

TEST(ExplorerPanel, NewFolderCopyPasteRenameAndUndo) {
    const ProjectDir dir;
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::ExplorerPanel panel{selection, history};
    panel.open(dir.root());
    panel.toggle("assets");
    panel.select("assets");

    panel.new_folder();
    EXPECT_TRUE(std::filesystem::is_directory(dir.root() / "assets" / "New Folder"));
    EXPECT_EQ(panel.renaming(), "assets/New Folder");
    EXPECT_EQ(panel.selected(), "assets/New Folder");
    EXPECT_TRUE(panel.undo().ok);
    EXPECT_FALSE(std::filesystem::exists(dir.root() / "assets" / "New Folder"));

    panel.toggle("assets");
    panel.toggle("assets/ui");
    panel.select("assets/ui/menu.xml");
    panel.copy();
    panel.paste();
    EXPECT_TRUE(std::filesystem::exists(dir.root() / "assets" / "ui" / "menu 2.xml"));
    EXPECT_EQ(panel.selected(), "assets/ui/menu 2.xml");
    EXPECT_TRUE(panel.undo().ok);
    EXPECT_FALSE(std::filesystem::exists(dir.root() / "assets" / "ui" / "menu 2.xml"));

    panel.select("src/game.cpp");
    panel.begin_rename();
    ASSERT_TRUE(row_of(*panel.view_model(), "src/game.cpp"));
    row_of(*panel.view_model(), "src/game.cpp")->editText = "play.cpp";
    panel.commit_rename();
    EXPECT_TRUE(std::filesystem::exists(dir.root() / "src" / "play.cpp"));
    EXPECT_FALSE(std::filesystem::exists(dir.root() / "src" / "game.cpp"));
    EXPECT_EQ(panel.selected(), "src/play.cpp");
    const std::vector<editor::ProjectRelocation> moved = panel.take_relocations();
    ASSERT_EQ(moved.size(), 1u);
    EXPECT_EQ(moved[0].from, "src/game.cpp");
    EXPECT_EQ(moved[0].to, "src/play.cpp");

    panel.select("src");
    panel.cut_key("src/play.cpp");
    panel.paste_at("src");
    EXPECT_TRUE(std::filesystem::exists(dir.root() / "src" / "play.cpp"));
    panel.new_header();
    EXPECT_TRUE(panel.take_notice().find("CMakeLists.txt") != std::string::npos);
    EXPECT_TRUE(std::filesystem::exists(dir.root() / "src" / "New Header.hpp"));
}

TEST(ExplorerPanel, DropFolderRejectsTheSameDirectoryAndADescendant) {
    const ProjectDir dir;
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::ExplorerPanel panel{selection, history};
    panel.open(dir.root());
    EXPECT_EQ(panel.drop_folder("assets/ui/menu.xml", "src"), "src");
    EXPECT_FALSE(panel.drop_folder("assets/ui/menu.xml", "assets/ui"));
    EXPECT_FALSE(panel.drop_folder("assets", "assets/ui"));
    EXPECT_FALSE(panel.drop_folder("wind_project.toml", "src"));

    panel.move_into("src/game.cpp", "assets");
    EXPECT_TRUE(std::filesystem::exists(dir.root() / "assets" / "game.cpp"));
    EXPECT_FALSE(std::filesystem::exists(dir.root() / "src" / "game.cpp"));
}

TEST(ExplorerPanel, DeleteRemovesTheFileAndUndoPutsItBack) {
    const ProjectDir dir;
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::ExplorerPanel panel{selection, history};
    panel.open(dir.root());
    panel.select("src/game.cpp");
    panel.remove();
    EXPECT_FALSE(std::filesystem::exists(dir.root() / "src" / "game.cpp"));
    EXPECT_EQ(panel.selected(), "src");
    const std::vector<std::string> removed = panel.take_removals();
    ASSERT_EQ(removed.size(), 1u);
    EXPECT_EQ(removed[0], "src/game.cpp");
    EXPECT_TRUE(panel.undo().ok);
    EXPECT_TRUE(std::filesystem::exists(dir.root() / "src" / "game.cpp"));

    panel.select("wind_project.toml");
    panel.remove();
    EXPECT_TRUE(std::filesystem::exists(dir.root() / "wind_project.toml"));
    EXPECT_NE(panel.take_notice().find("wind_project.toml"), std::string::npos);
}

TEST(ExplorerPanel, GridShowsOneFolderAndStepsBack) {
    const ProjectDir dir;
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::ExplorerPanel panel{selection, history};
    panel.open(dir.root());
    editor::ExplorerViewModel& vm = *panel.view_model();

    EXPECT_EQ(vm.viewIcon.get(), assets::icons::view_grid);
    panel.show_grid();
    EXPECT_EQ(vm.viewIcon.get(), assets::icons::view_list);
    EXPECT_EQ(panel.view(), editor::ExplorerView::Grid);
    EXPECT_TRUE(panel.grid_folder().empty());
    EXPECT_TRUE(vm.rows.get().empty());
    EXPECT_EQ(tile_names(vm), (std::vector<std::string>{"assets", "src", "CMakeLists.txt", "wind_project.toml"}));
    EXPECT_FALSE(panel.can_grid_up());
    EXPECT_TRUE(panel.take_grid_scroll_reset());

    EXPECT_TRUE(panel.open_entry("assets"));
    EXPECT_EQ(panel.grid_folder(), "assets");
    EXPECT_EQ(tile_names(vm), (std::vector<std::string>{"Images", "ui"}));
    EXPECT_NE(vm.rootText.get().find("assets"), std::string::npos);
    EXPECT_TRUE(panel.can_grid_up());
    EXPECT_FALSE(panel.open_entry("assets/ui/menu.xml")) << "a file is not a folder";
    EXPECT_EQ(panel.grid_folder(), "assets");

    panel.grid_up();
    EXPECT_TRUE(panel.grid_folder().empty());
    EXPECT_EQ(tile_names(vm), (std::vector<std::string>{"assets", "src", "CMakeLists.txt", "wind_project.toml"}));
    EXPECT_FALSE(panel.can_grid_up());

    EXPECT_TRUE(panel.open_entry("assets"));
    panel.show_grid();
    EXPECT_EQ(panel.grid_folder(), "assets") << "Grid, while the grid is showing, stays in the open folder";
}

TEST(ExplorerPanel, GridFollowsTheSelectionAndReturnsToTheTree) {
    const ProjectDir dir;
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::ExplorerPanel panel{selection, history};
    panel.open(dir.root());
    panel.select("assets/ui/menu.xml");

    panel.show_grid();
    editor::ExplorerViewModel& vm = *panel.view_model();
    EXPECT_EQ(panel.grid_folder(), "assets/ui");
    EXPECT_EQ(tile_names(vm), (std::vector<std::string>{"menu.xml"}));
    EXPECT_NE(vm.rootText.get().find("assets/ui"), std::string::npos);
    EXPECT_EQ(panel.selected(), "assets/ui/menu.xml");

    panel.show_tree();
    EXPECT_EQ(vm.viewIcon.get(), assets::icons::view_grid);
    EXPECT_EQ(panel.view(), editor::ExplorerView::Tree);
    EXPECT_TRUE(vm.tiles.get().empty());
    EXPECT_EQ(panel.selected(), "assets/ui/menu.xml");
    EXPECT_TRUE(row_of(vm, "assets")->expanded.get());
    EXPECT_TRUE(row_of(vm, "assets/ui")->expanded.get());
    EXPECT_EQ(row_of(vm, "assets/ui/menu.xml")->label.get(), "menu.xml");
}

TEST(ExplorerPanel, GridRescanWalksUpWhenTheFolderIsGone) {
    const ProjectDir dir;
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::ExplorerPanel panel{selection, history};
    panel.open(dir.root());
    panel.show_grid();
    EXPECT_TRUE(panel.open_entry("assets/ui"));
    EXPECT_TRUE(panel.take_grid_scroll_reset());
    std::filesystem::remove_all(dir.root() / "assets" / "ui");
    panel.rescan();
    EXPECT_EQ(panel.grid_folder(), "assets");
    EXPECT_EQ(tile_names(*panel.view_model()), (std::vector<std::string>{"Images"}));
    EXPECT_TRUE(panel.take_grid_scroll_reset());
}

TEST(ExplorerPanel, GridKeysStepByColumns) {
    const ProjectDir dir;
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::ExplorerPanel panel{selection, history};
    panel.open(dir.root());
    panel.show_grid();
    panel.set_grid_columns(3);

    EXPECT_FALSE(panel.navigate(engine::ui::TreeNav::Right));
    EXPECT_EQ(panel.navigate(engine::ui::TreeNav::Down), 0u);
    EXPECT_EQ(panel.selected(), "assets");
    EXPECT_EQ(panel.navigate(engine::ui::TreeNav::Down), 3u);
    EXPECT_EQ(panel.selected(), "wind_project.toml");
    EXPECT_EQ(panel.navigate(engine::ui::TreeNav::Up), 0u);
    EXPECT_EQ(panel.selected(), "assets");
    EXPECT_EQ(panel.navigate(engine::ui::TreeNav::Right), 1u);
    EXPECT_EQ(panel.selected(), "src");
    EXPECT_EQ(panel.navigate(engine::ui::TreeNav::Left), 0u);
    EXPECT_EQ(panel.navigate(engine::ui::TreeNav::Last), 3u);
    EXPECT_EQ(panel.selected(), "wind_project.toml");
    EXPECT_TRUE(panel.grid_tile_scroll_pending());
}

TEST(ExplorerPanel, GridEmptySpaceCreatesAndAcceptsADropInTheOpenFolder) {
    const ProjectDir dir;
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::ExplorerPanel panel{selection, history};
    panel.open(dir.root());
    panel.show_grid();
    EXPECT_TRUE(panel.open_entry("assets"));

    panel.view_model()->blankNewFolder.execute();
    EXPECT_TRUE(std::filesystem::is_directory(dir.root() / "assets" / "New Folder"));
    EXPECT_EQ(panel.selected(), "assets/New Folder");
    EXPECT_EQ(panel.renaming(), "assets/New Folder");
    EXPECT_EQ(panel.grid_folder(), "assets");

    EXPECT_EQ(panel.drop_folder("src/game.cpp", ""), "assets");
    EXPECT_FALSE(panel.drop_folder("assets/Images", "")) << "the file is already in this folder";
}

TEST(ExplorerPanel, OpeningAnotherProjectKeepsGridModeAndStartsAtTheRoot) {
    const ProjectDir dir;
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::ExplorerPanel panel{selection, history};
    panel.open(dir.root());
    panel.show_grid();
    EXPECT_TRUE(panel.open_entry("src"));
    panel.open(dir.root());
    EXPECT_EQ(panel.view(), editor::ExplorerView::Grid);
    EXPECT_TRUE(panel.grid_folder().empty());
    EXPECT_TRUE(panel.view_model()->rows.get().empty());
    EXPECT_EQ(tile_names(*panel.view_model()).size(), 4u);
}

TEST(ExplorerPanel, ExternalRescanDropsHistoryWhenTheFileIsGone) {
    const ProjectDir dir;
    editor::EditorSelection selection;
    editor::CommandHistory history;
    editor::ExplorerPanel panel{selection, history};
    panel.open(dir.root());
    panel.select("src");
    panel.new_folder();
    panel.cancel_rename();
    std::filesystem::remove(dir.root() / "src" / "New Folder");
    panel.rescan();
    EXPECT_FALSE(panel.undo().ok);
}
