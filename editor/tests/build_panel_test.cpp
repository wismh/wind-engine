#include <gtest/gtest.h>

#include "build_panel.h"

#include <string>
#include <vector>

TEST(BuildPanel, TonesErrorsAndWarningsOfEveryTool) {
    EXPECT_EQ(editor::tone_of("C:/game/src/game.cpp(43,26): error C2512: no default constructor"),
            editor::LineTone::Error);
    EXPECT_EQ(editor::tone_of("main.obj : error LNK2019: unresolved external symbol"), editor::LineTone::Error);
    EXPECT_EQ(editor::tone_of("game.cpp(1): fatal error C1083: Cannot open include file"), editor::LineTone::Error);
    EXPECT_EQ(editor::tone_of("CMake Error at CMakeLists.txt:3 (find_package):"), editor::LineTone::Error);
    EXPECT_EQ(editor::tone_of("stb_image.h(4972,22): warning C4244: '=': conversion"), editor::LineTone::Warning);
    EXPECT_EQ(editor::tone_of("CMake Warning (dev) at CMakeLists.txt:1:"), editor::LineTone::Warning);
    EXPECT_EQ(editor::tone_of("    0 Error(s)"), editor::LineTone::Plain);
    EXPECT_EQ(editor::tone_of("my_game.vcxproj -> C:/game/my_game.dll"), editor::LineTone::Plain);
}

TEST(BuildPanel, AppendsScrollsToTheEndAndRemembersTheFirstError) {
    editor::BuildPanel panel;
    editor::BuildViewModel& vm = *panel.view_model();
    panel.append({"> cmake --build build-editor", "a.cpp(1): error C1: first", "b.cpp(2): error C2: second"});
    ASSERT_EQ(vm.lines.get().size(), 3u);
    EXPECT_EQ(vm.lines.get()[1]->text.get(), "a.cpp(1): error C1: first");
    EXPECT_FALSE(vm.lines.get()[1]->tone.get().empty());
    EXPECT_TRUE(vm.lines.get()[0]->tone.get().empty());
    EXPECT_GT(vm.logScroll.get(), 1.0e6f);
    EXPECT_EQ(panel.first_error(), "a.cpp(1): error C1: first");

    panel.clear();
    EXPECT_TRUE(vm.lines.get().empty());
    EXPECT_TRUE(panel.first_error().empty());
    EXPECT_EQ(vm.logScroll.get(), 0.0f);
}

TEST(BuildPanel, KeepsOnlyTheLastLines) {
    editor::BuildPanel panel;
    std::vector<std::string> lines;
    for (std::size_t i = 0; i < editor::BuildPanel::kMaxLines + 10; ++i) {
        lines.push_back("line " + std::to_string(i));
    }
    panel.append(std::move(lines));
    const auto& shown = panel.view_model()->lines.get();
    ASSERT_EQ(shown.size(), editor::BuildPanel::kMaxLines);
    EXPECT_EQ(shown.front()->text.get(), "line 10");
}
