#include <gtest/gtest.h>

#include "explorer_panel.h"
#include "project_scan.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
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

TEST(ExplorerPanel, NoProjectShowsNothingAndCannotRefresh) {
    editor::ExplorerPanel panel;
    const editor::ExplorerViewModel& vm = *panel.view_model();
    EXPECT_TRUE(vm.rows.get().empty());
    EXPECT_EQ(vm.rootText.get(), "No project open.");
    EXPECT_FALSE(vm.refresh.can_execute());
}

TEST(ExplorerPanel, OpenShowsTheCollapsedRootsAndExpandsOnToggle) {
    const ProjectDir dir;
    editor::ExplorerPanel panel;
    panel.open(dir.root());
    editor::ExplorerViewModel& vm = *panel.view_model();
    EXPECT_TRUE(vm.refresh.can_execute());
    EXPECT_EQ(labels(vm), (std::vector<std::string>{"assets", "src", "CMakeLists.txt", "wind_project.toml"}));
    EXPECT_EQ(vm.detail.get(), "Select a file or folder.");
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

TEST(ExplorerPanel, SelectShowsThePathAndWhatItIs) {
    const ProjectDir dir;
    editor::ExplorerPanel panel;
    panel.open(dir.root());
    editor::ExplorerViewModel& vm = *panel.view_model();
    panel.toggle("src");
    row_of(vm, "src/game.cpp")->select.execute();
    EXPECT_EQ(panel.selected(), "src/game.cpp");
    EXPECT_EQ(vm.detail.get(), "src/game.cpp\nFile, 2.0 KB");
    EXPECT_NE(row_of(vm, "src/game.cpp")->rowFill.get(), row_of(vm, "src")->rowFill.get());

    row_of(vm, "src")->select.execute();
    EXPECT_EQ(vm.detail.get(), "src\nFolder, 1 item");
    panel.select("CMakeLists.txt");
    EXPECT_EQ(vm.detail.get(), "CMakeLists.txt\nFile, 0 bytes");
    panel.select("no/such");
    EXPECT_EQ(panel.selected(), "CMakeLists.txt") << "an unknown key leaves the selection";
}

TEST(ExplorerPanel, RowsKeepTheirViewModelWhileTheTreeChanges) {
    const ProjectDir dir;
    editor::ExplorerPanel panel;
    panel.open(dir.root());
    editor::ExplorerViewModel& vm = *panel.view_model();
    const std::shared_ptr<editor::ExplorerRowViewModel> src = row_of(vm, "src");
    panel.toggle("assets");
    EXPECT_EQ(row_of(vm, "src"), src);
}

TEST(ExplorerPanel, TreeKeysMoveExpandAndCollapse) {
    const ProjectDir dir;
    editor::ExplorerPanel panel;
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
    editor::ExplorerPanel panel;
    panel.open(dir.root());
    editor::ExplorerViewModel& vm = *panel.view_model();
    panel.toggle("assets");
    panel.toggle("assets/ui");
    panel.select("assets/ui/menu.xml");

    dir.write("assets/ui/hud.xml", "<Canvas/>");
    vm.refresh.execute();
    EXPECT_EQ(labels(vm), (std::vector<std::string>{"assets", "  Images", "  ui", "    hud.xml", "    menu.xml",
                                  "src", "CMakeLists.txt", "wind_project.toml"}));
    EXPECT_EQ(panel.selected(), "assets/ui/menu.xml");

    std::filesystem::remove(dir.root() / "assets/ui/menu.xml");
    vm.refresh.execute();
    EXPECT_TRUE(panel.selected().empty()) << "the selected file is gone";
    EXPECT_EQ(vm.detail.get(), "Select a file or folder.");
    EXPECT_TRUE(row_of(vm, "assets/ui")->expanded.get());
}

TEST(ExplorerPanel, OpenAgainStartsCollapsedAndCloseClears) {
    const ProjectDir dir;
    editor::ExplorerPanel panel;
    panel.open(dir.root());
    panel.toggle("assets");
    panel.select("assets");
    panel.open(dir.root());
    editor::ExplorerViewModel& vm = *panel.view_model();
    EXPECT_EQ(vm.rows.get().size(), 4u);
    EXPECT_TRUE(panel.selected().empty());

    panel.close();
    EXPECT_TRUE(vm.rows.get().empty());
    EXPECT_EQ(vm.rootText.get(), "No project open.");
    EXPECT_FALSE(vm.refresh.can_execute());
}
