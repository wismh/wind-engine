#include <gtest/gtest.h>

#include "editor_options.h"

#include <array>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

TEST(EditorOptions, ProjectAndPlay) {
    std::array<char*, 4> args{const_cast<char*>("--project"), const_cast<char*>("C:/games/ttt"),
            const_cast<char*>("--play"), const_cast<char*>("--game")};
    const editor::EditorOptions options = editor::parse_editor_options(std::span<char* const>(args));
    ASSERT_TRUE(options.project.has_value());
    EXPECT_EQ(*options.project, std::filesystem::path("C:/games/ttt"));
    EXPECT_TRUE(options.play);
    ASSERT_EQ(options.unknown.size(), 1u);
    EXPECT_EQ(options.unknown[0], "--game");
}

TEST(EditorOptions, ProjectWithoutADirectoryIsUnknown) {
    std::array<char*, 1> args{const_cast<char*>("--project")};
    const editor::EditorOptions options = editor::parse_editor_options(std::span<char* const>(args));
    EXPECT_FALSE(options.project.has_value());
    ASSERT_EQ(options.unknown.size(), 1u);
}

namespace {

editor::EditorOptions parse(std::initializer_list<const char*> words) {
    std::vector<char*> args;
    for (const char* word : words) {
        args.push_back(const_cast<char*>(word));
    }
    return editor::parse_editor_options(std::span<char* const>(args));
}

}

TEST(EditorOptions, BatchExportWithADirectoryAndALogFile) {
    const editor::EditorOptions options = parse({"--batch", "--project", "C:/games/ttt", "--export", "D:/out/ttt",
            "--log-file", "D:/out/export.log"});
    EXPECT_TRUE(options.batch);
    EXPECT_TRUE(options.export_game);
    ASSERT_TRUE(options.export_directory.has_value());
    EXPECT_EQ(*options.export_directory, std::filesystem::path("D:/out/ttt"));
    ASSERT_TRUE(options.log_file.has_value());
    EXPECT_EQ(*options.log_file, std::filesystem::path("D:/out/export.log"));
    EXPECT_TRUE(options.unknown.empty());
    EXPECT_TRUE(editor::usage_error(options).empty());
}

TEST(EditorOptions, ExportWithoutADirectoryMeansTheDefault) {
    // At the end, and before another option.
    editor::EditorOptions options = parse({"--batch", "--project", "p", "--export"});
    EXPECT_TRUE(options.export_game);
    EXPECT_FALSE(options.export_directory.has_value());
    EXPECT_TRUE(editor::usage_error(options).empty());

    options = parse({"--batch", "--export", "--project", "p"});
    EXPECT_TRUE(options.export_game);
    EXPECT_FALSE(options.export_directory.has_value());
    ASSERT_TRUE(options.project.has_value());
    EXPECT_TRUE(editor::usage_error(options).empty());
}

TEST(EditorOptions, BatchNeedsACommandAndAProject) {
    EXPECT_NE(editor::usage_error(parse({"--batch", "--project", "p"})).find("--export"), std::string::npos);
    EXPECT_NE(editor::usage_error(parse({"--batch", "--export"})).find("--project"), std::string::npos);
    EXPECT_NE(editor::usage_error(parse({"--export", "--project", "p"})).find("--batch"), std::string::npos);
    EXPECT_TRUE(editor::usage_error(parse({"--project", "p", "--play"})).empty());
    EXPECT_TRUE(editor::usage_error(parse({})).empty());
}

TEST(EditorOptions, LogFileWithoutAPathAndUnknownBatchOptionsAreUnknown) {
    const editor::EditorOptions options = parse({"--batch", "--bogus", "--log-file"});
    EXPECT_FALSE(options.log_file.has_value());
    ASSERT_EQ(options.unknown.size(), 2u);
    EXPECT_EQ(options.unknown[0], "--bogus");
    EXPECT_EQ(options.unknown[1], "--log-file");
}
