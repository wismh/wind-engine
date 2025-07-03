#include <gtest/gtest.h>

#include "editor_options.h"

#include <array>
#include <span>

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
