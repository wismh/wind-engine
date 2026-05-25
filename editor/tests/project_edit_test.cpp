#include <gtest/gtest.h>

#include "project_edit.h"

#include <engine/resources/meta.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

namespace {

class Tree {
public:
    Tree()
        : root_(std::filesystem::temp_directory_path() / "wind_project_edit_test" /
                  ::testing::UnitTest::GetInstance()->current_test_info()->name()) {
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_ / "assets" / "ui");
        std::filesystem::create_directories(root_ / "src");
        write("wind_project.toml", "name = \"x\"\n");
        write("assets/ui/menu.xml", "<Canvas/>");
        write("assets/ui/menu.xml.meta",
                "guid = \"0123456789abcdef0123456789abcdef\"\nimporter = \"ui\"\n");
        write("src/game.cpp", "int main(){return 0;}\n");
    }
    ~Tree() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }
    Tree(const Tree&) = delete;
    Tree& operator=(const Tree&) = delete;

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

    void write(const std::string& key, const std::string& text) const {
        const std::filesystem::path path = root_ / key;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    }

    [[nodiscard]] std::string read(const std::string& key) const {
        std::ifstream in(root_ / key, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    [[nodiscard]] bool exists(const std::string& key) const { return std::filesystem::exists(root_ / key); }

    [[nodiscard]] std::optional<std::string> guid(const std::string& key) const {
        const auto parsed = engine::parse_asset_meta(read(key));
        if (!parsed) {
            return std::nullopt;
        }
        return std::string(parsed->guid.hex());
    }

private:
    std::filesystem::path root_;
};

}

TEST(ProjectEdit, RelocateKeepsTheGuidAndUndoPutsItBack) {
    Tree tree;
    editor::ProjectHistory history;
    const editor::ProjectEditResult moved =
            history.relocate(tree.root(), "assets/ui/menu.xml", "assets/ui/hud.xml");
    EXPECT_TRUE(moved.ok);
    EXPECT_EQ(moved.select, "assets/ui/hud.xml");
    ASSERT_EQ(moved.moved.size(), 1u);
    EXPECT_EQ(moved.moved[0].from, "assets/ui/menu.xml");
    EXPECT_EQ(moved.moved[0].to, "assets/ui/hud.xml");
    EXPECT_FALSE(tree.exists("assets/ui/menu.xml"));
    EXPECT_FALSE(tree.exists("assets/ui/menu.xml.meta"));
    EXPECT_EQ(tree.read("assets/ui/hud.xml"), "<Canvas/>");
    EXPECT_EQ(tree.guid("assets/ui/hud.xml.meta").value_or(""), "0123456789abcdef0123456789abcdef");
    EXPECT_TRUE(history.matches(tree.root()));

    const editor::ProjectEditResult back = history.undo(tree.root());
    EXPECT_TRUE(back.ok);
    EXPECT_EQ(back.select, "assets/ui/menu.xml");
    ASSERT_EQ(back.moved.size(), 1u);
    EXPECT_EQ(back.moved[0].from, "assets/ui/hud.xml");
    EXPECT_EQ(back.moved[0].to, "assets/ui/menu.xml");
    EXPECT_EQ(tree.guid("assets/ui/menu.xml.meta").value_or(""), "0123456789abcdef0123456789abcdef");
    EXPECT_FALSE(tree.exists("assets/ui/hud.xml"));

    const editor::ProjectEditResult again = history.redo(tree.root());
    EXPECT_TRUE(again.ok);
    EXPECT_TRUE(tree.exists("assets/ui/hud.xml.meta"));
    EXPECT_FALSE(tree.exists("assets/ui/menu.xml"));
}

TEST(ProjectEdit, RelocateRefusesTheProjectFileADescendantAndATakenName) {
    Tree tree;
    tree.write("assets/ui/hud.xml", "x");
    editor::ProjectHistory history;
    EXPECT_FALSE(history.relocate(tree.root(), "wind_project.toml", "moved.toml").ok);
    EXPECT_TRUE(tree.exists("wind_project.toml"));
    EXPECT_FALSE(history.relocate(tree.root(), "assets", "assets/ui/nested").ok);
    EXPECT_TRUE(tree.exists("assets/ui/menu.xml"));
    EXPECT_FALSE(history.relocate(tree.root(), "assets/ui/menu.xml", "assets/ui/hud.xml").ok);
    EXPECT_FALSE(history.relocate(tree.root(), "assets/ui/menu.xml", "../outside.xml").ok);
    EXPECT_FALSE(history.relocate(tree.root(), "assets/ui/menu.xml", "assets/ui/bad:name.xml").ok);
    EXPECT_TRUE(history.matches(tree.root()));
    EXPECT_FALSE(history.can_undo());
}

TEST(ProjectEdit, CopyWritesANewGuidAndUndoRemovesOnlyTheCopy) {
    Tree tree;
    editor::ProjectHistory history;
    const editor::ProjectEditResult copied =
            history.copy_entry(tree.root(), "assets/ui/menu.xml", "assets/ui/menu_copy.xml");
    EXPECT_TRUE(copied.ok);
    EXPECT_EQ(tree.read("assets/ui/menu_copy.xml"), "<Canvas/>");
    const std::optional<std::string> copy_guid = tree.guid("assets/ui/menu_copy.xml.meta");
    ASSERT_TRUE(copy_guid);
    EXPECT_NE(*copy_guid, "0123456789abcdef0123456789abcdef");
    EXPECT_EQ(tree.guid("assets/ui/menu.xml.meta").value_or(""), "0123456789abcdef0123456789abcdef");

    EXPECT_TRUE(history.undo(tree.root()).ok);
    EXPECT_FALSE(tree.exists("assets/ui/menu_copy.xml"));
    EXPECT_FALSE(tree.exists("assets/ui/menu_copy.xml.meta"));
    EXPECT_TRUE(tree.exists("assets/ui/menu.xml"));
}

TEST(ProjectEdit, CopyOfAFolderOutsideAssetsDoesNotInventASidecar) {
    Tree tree;
    editor::ProjectHistory history;
    EXPECT_TRUE(history.copy_entry(tree.root(), "src/game.cpp", "src/game_copy.cpp").ok);
    EXPECT_EQ(tree.read("src/game_copy.cpp"), "int main(){return 0;}\n");
    EXPECT_FALSE(tree.exists("src/game_copy.cpp.meta"));
    EXPECT_TRUE(history.undo(tree.root()).ok);
    EXPECT_FALSE(tree.exists("src/game_copy.cpp"));
}

TEST(ProjectEdit, CreateWorldHeaderAndFolderThenUndo) {
    Tree tree;
    editor::ProjectHistory history;
    const editor::ProjectEditResult world =
            history.create(tree.root(), "assets/ui/New World.world", editor::ProjectCreateKind::World);
    EXPECT_TRUE(world.ok);
    EXPECT_EQ(tree.read("assets/ui/New World.world"), "version = 1\n");
    EXPECT_NE(tree.read("assets/ui/New World.world.meta").find("importer = \"world\""), std::string::npos);

    const editor::ProjectEditResult header =
            history.create(tree.root(), "src/New Header.hpp", editor::ProjectCreateKind::Header);
    EXPECT_TRUE(header.ok);
    EXPECT_TRUE(header.notice.find("CMakeLists.txt") != std::string::npos);
    EXPECT_TRUE(tree.exists("src/New Header.hpp"));
    EXPECT_FALSE(tree.exists("src/New Header.hpp.meta"));

    EXPECT_TRUE(history.create(tree.root(), "src/New Folder", editor::ProjectCreateKind::Directory).ok);
    EXPECT_TRUE(std::filesystem::is_directory(tree.root() / "src" / "New Folder"));

    EXPECT_TRUE(history.undo(tree.root()).ok);
    EXPECT_FALSE(tree.exists("src/New Folder"));
    EXPECT_TRUE(history.undo(tree.root()).ok);
    EXPECT_FALSE(tree.exists("src/New Header.hpp"));
    EXPECT_TRUE(history.undo(tree.root()).ok);
    EXPECT_FALSE(tree.exists("assets/ui/New World.world"));
    EXPECT_FALSE(tree.exists("assets/ui/New World.world.meta"));
}

TEST(ProjectEdit, UndoOfACreateRefusesOnceTheFileChanged) {
    Tree tree;
    editor::ProjectHistory history;
    ASSERT_TRUE(history.create(tree.root(), "src/New Source.cpp", editor::ProjectCreateKind::Source).ok);
    tree.write("src/New Source.cpp", "changed\n");
    const editor::ProjectEditResult undone = history.undo(tree.root());
    EXPECT_FALSE(undone.ok);
    EXPECT_TRUE(tree.exists("src/New Source.cpp"));
    EXPECT_TRUE(history.can_undo());
}

TEST(ProjectEdit, ExternalChangeDropsTheMatch) {
    Tree tree;
    editor::ProjectHistory history;
    ASSERT_TRUE(history.relocate(tree.root(), "src/game.cpp", "src/play.cpp").ok);
    std::filesystem::remove(tree.root() / "src" / "play.cpp");
    EXPECT_FALSE(history.matches(tree.root()));
}

TEST(ProjectEdit, DeleteRestoresTheFileTheGuidAndTheFolder) {
    Tree tree;
    editor::ProjectHistory history;
    const editor::ProjectEditResult removed = history.remove(tree.root(), "assets/ui/menu.xml");
    EXPECT_TRUE(removed.ok);
    EXPECT_EQ(removed.select, "assets/ui");
    ASSERT_EQ(removed.removed.size(), 1u);
    EXPECT_EQ(removed.removed[0], "assets/ui/menu.xml");
    EXPECT_FALSE(tree.exists("assets/ui/menu.xml"));
    EXPECT_FALSE(tree.exists("assets/ui/menu.xml.meta"));

    const editor::ProjectEditResult back = history.undo(tree.root());
    EXPECT_TRUE(back.ok);
    EXPECT_EQ(back.select, "assets/ui/menu.xml");
    EXPECT_TRUE(back.removed.empty());
    EXPECT_EQ(tree.read("assets/ui/menu.xml"), "<Canvas/>");
    EXPECT_EQ(tree.guid("assets/ui/menu.xml.meta").value_or(""), "0123456789abcdef0123456789abcdef");

    EXPECT_TRUE(history.remove(tree.root(), "assets/ui").ok);
    EXPECT_FALSE(tree.exists("assets/ui/menu.xml"));
    EXPECT_TRUE(history.undo(tree.root()).ok);
    EXPECT_EQ(tree.read("assets/ui/menu.xml"), "<Canvas/>");
    EXPECT_EQ(tree.guid("assets/ui/menu.xml.meta").value_or(""), "0123456789abcdef0123456789abcdef");
}

TEST(ProjectEdit, DeleteRefusesTheProjectFileAndATakenNameOnUndo) {
    Tree tree;
    editor::ProjectHistory history;
    EXPECT_FALSE(history.remove(tree.root(), "wind_project.toml").ok);
    EXPECT_TRUE(tree.exists("wind_project.toml"));
    ASSERT_TRUE(history.remove(tree.root(), "src/game.cpp").ok);
    tree.write("src/game.cpp", "taken\n");
    const editor::ProjectEditResult undone = history.undo(tree.root());
    EXPECT_FALSE(undone.ok);
    EXPECT_EQ(tree.read("src/game.cpp"), "taken\n");
    EXPECT_TRUE(history.can_undo());
}

TEST(ProjectNames, UniqueNameAndRejectedCharacters) {
    Tree tree;
    EXPECT_FALSE(editor::project_name_ok(""));
    EXPECT_FALSE(editor::project_name_ok("a/b"));
    EXPECT_FALSE(editor::project_name_ok("a."));
    EXPECT_FALSE(editor::project_name_ok("a:b"));
    EXPECT_TRUE(editor::project_name_ok("New Folder"));
    EXPECT_EQ(editor::project_unique_name(tree.root() / "src", "game", ".cpp"), "game 2.cpp");
    EXPECT_EQ(editor::project_parent_key("assets/ui/menu.xml"), "assets/ui");
    EXPECT_EQ(editor::project_join_key("assets/ui", "hud.xml"), "assets/ui/hud.xml");
}
