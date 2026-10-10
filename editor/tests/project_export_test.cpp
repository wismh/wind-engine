#include <gtest/gtest.h>

#include "project_export.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;

void write(const fs::path& path, const std::string& text = "x") {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// A build output and an export target under the temp directory, removed with the object.
class Dirs {
public:
    Dirs()
        : root_(fs::temp_directory_path() / "wind_project_export_test" /
                  ::testing::UnitTest::GetInstance()->current_test_info()->name()) {
        fs::remove_all(root_);
        fs::create_directories(root_);
    }
    ~Dirs() {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    [[nodiscard]] fs::path from() const {
        return root_ / "build" / "bin" / "Release";
    }
    [[nodiscard]] fs::path to() const {
        return root_ / "project" / "export" / "my_game";
    }
    [[nodiscard]] fs::path project() const {
        return root_ / "project";
    }

private:
    fs::path root_;
};

}

TEST(ProjectExport, CopiesTheExecutableAndItsAssetsAndSkipsBuildLeftovers) {
    const Dirs dirs;
    write(dirs.from() / "my_game.exe", "exe");
    write(dirs.from() / "assets" / "catalog.toml", "catalog");
    write(dirs.from() / "assets" / "engine" / "ui" / "theme.bin", "theme");
    write(dirs.from() / "my_game.pdb");
    write(dirs.from() / "my_game.ilk");
    write(dirs.from() / "my_game.exp");
    write(dirs.from() / "my_game.lib");
    write(dirs.from() / "my_game.obj");
    write(dirs.from() / "libengine.a");
    write(dirs.from() / "CMakeFiles" / "deep" / "state.txt");

    const auto result = editor::copy_export(dirs.from(), dirs.to());
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_TRUE(fs::equivalent(*result, dirs.to()));
    EXPECT_TRUE(fs::exists(dirs.to() / "my_game.exe"));
    EXPECT_TRUE(fs::exists(dirs.to() / "assets" / "catalog.toml"));
    EXPECT_TRUE(fs::exists(dirs.to() / "assets" / "engine" / "ui" / "theme.bin"));
    for (const char* skipped : {"my_game.pdb", "my_game.ilk", "my_game.exp", "my_game.lib", "my_game.obj",
                 "libengine.a", "CMakeFiles"}) {
        EXPECT_FALSE(fs::exists(dirs.to() / skipped)) << skipped;
    }
    std::ifstream in(dirs.to() / "my_game.exe", std::ios::binary);
    std::string text;
    in >> text;
    EXPECT_EQ(text, "exe");
}

TEST(ProjectExport, ReplacesWhatWasInTheTargetBefore) {
    const Dirs dirs;
    write(dirs.from() / "my_game.exe", "new");
    write(dirs.to() / editor::kExportMarker);
    write(dirs.to() / "my_game.exe", "old");
    write(dirs.to() / "stale" / "removed.txt");

    ASSERT_TRUE(editor::copy_export(dirs.from(), dirs.to()).has_value());
    EXPECT_FALSE(fs::exists(dirs.to() / "stale"));
    EXPECT_TRUE(fs::exists(dirs.to() / editor::kExportMarker));
    std::ifstream in(dirs.to() / "my_game.exe", std::ios::binary);
    std::string text;
    in >> text;
    EXPECT_EQ(text, "new");
}

TEST(ProjectExport, MarksItsTargetSoTheNextExportMayEmptyIt) {
    const Dirs dirs;
    write(dirs.from() / "my_game.exe", "exe");
    ASSERT_TRUE(editor::copy_export(dirs.from(), dirs.to()).has_value());
    EXPECT_TRUE(fs::exists(dirs.to() / editor::kExportMarker));
    EXPECT_TRUE(editor::export_directory_problem(dirs.project(), dirs.to()).empty());
    ASSERT_TRUE(editor::copy_export(dirs.from(), dirs.to()).has_value());
}

TEST(ProjectExport, UsesAnEmptyExistingDirectory) {
    const Dirs dirs;
    write(dirs.from() / "my_game.exe", "exe");
    fs::create_directories(dirs.to());
    EXPECT_TRUE(editor::export_directory_problem(dirs.project(), dirs.to()).empty());
    ASSERT_TRUE(editor::copy_export(dirs.from(), dirs.to()).has_value());
    EXPECT_TRUE(fs::exists(dirs.to() / "my_game.exe"));
}

TEST(ProjectExport, NeverEmptiesADirectoryItDidNotMake) {
    const Dirs dirs;
    write(dirs.from() / "my_game.exe", "exe");
    write(dirs.to() / "photos" / "holiday.jpg", "precious");

    const std::string problem = editor::export_directory_problem(dirs.project(), dirs.to());
    EXPECT_NE(problem.find("not empty"), std::string::npos);
    const auto result = editor::copy_export(dirs.from(), dirs.to());
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().find(editor::kExportMarker), std::string::npos);
    EXPECT_TRUE(fs::exists(dirs.to() / "photos" / "holiday.jpg"));
    EXPECT_FALSE(fs::exists(dirs.to() / "my_game.exe"));
    EXPECT_FALSE(fs::exists(dirs.to() / editor::kExportMarker));

    // A file where the directory should be.
    fs::remove_all(dirs.to());
    write(dirs.to(), "a file");
    EXPECT_FALSE(editor::export_directory_problem(dirs.project(), dirs.to()).empty());
}

TEST(ProjectExport, FailsClearlyWhenThereIsNothingToCopyOrTheTargetOverlaps) {
    const Dirs dirs;
    auto missing = editor::copy_export(dirs.from(), dirs.to());
    ASSERT_FALSE(missing.has_value());
    EXPECT_NE(missing.error().find("not a directory"), std::string::npos);
    EXPECT_FALSE(fs::exists(dirs.to()));

    write(dirs.from() / "my_game.exe");
    auto inside = editor::copy_export(dirs.from(), dirs.from() / "out");
    ASSERT_FALSE(inside.has_value());
    EXPECT_NE(inside.error().find("overlap"), std::string::npos);
    auto same = editor::copy_export(dirs.from(), dirs.from());
    ASSERT_FALSE(same.has_value());
    EXPECT_TRUE(fs::exists(dirs.from() / "my_game.exe"));

    // A file where the target directory should be.
    write(dirs.to() / "blocker", "file");
    fs::remove_all(dirs.to());
    write(dirs.to(), "a file");
    auto blocked = editor::copy_export(dirs.from(), dirs.to() / "child");
    EXPECT_FALSE(blocked.has_value());
}

TEST(ProjectExport, TheDefaultDirectoryIsUnderTheProject) {
    EXPECT_EQ(editor::default_export_directory("C:/games/ttt", "ttt"), fs::path("C:/games/ttt/export/ttt"));
}

TEST(ProjectExport, RefusesTargetsThatWouldEmptyTheProject) {
    const Dirs dirs;
    fs::create_directories(dirs.project() / "src");
    EXPECT_TRUE(editor::export_directory_problem(dirs.project(), dirs.to()).empty());
    EXPECT_TRUE(editor::export_directory_problem(dirs.project(), dirs.project() / "export").empty());
    EXPECT_FALSE(editor::export_directory_problem(dirs.project(), dirs.project()).empty());
    EXPECT_FALSE(editor::export_directory_problem(dirs.project(), dirs.project().parent_path()).empty());
    EXPECT_FALSE(editor::export_directory_problem(dirs.project(), dirs.project().root_path()).empty());
    EXPECT_FALSE(editor::export_directory_problem(dirs.project(), {}).empty());
}

TEST(ProjectExport, SkipsByNameAndExtensionIgnoringCase) {
    EXPECT_TRUE(editor::skipped_in_export("bin/Game.PDB"));
    EXPECT_TRUE(editor::skipped_in_export("bin/CMakeFiles"));
    EXPECT_FALSE(editor::skipped_in_export("bin/game.exe"));
    EXPECT_FALSE(editor::skipped_in_export("bin/assets/catalog.toml"));
    EXPECT_FALSE(editor::skipped_in_export("bin/game"));
}
