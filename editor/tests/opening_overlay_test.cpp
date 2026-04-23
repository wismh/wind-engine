#include <gtest/gtest.h>

#include "opening_overlay.h"

#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>

#include <string>
#include <vector>

namespace {

constexpr engine::WindowId kEditorWindow{1};

}

TEST(OpeningOverlay, SpawnsClosedWithFixedEmptyRectAndFillsWindowWhenOpen) {
    engine::ecs::World world;
    editor::OpeningOverlay overlay;
    overlay.spawn(world, kEditorWindow);

    const auto& canvas = world.get<engine::ui::UiCanvas>(overlay.canvas());
    EXPECT_FALSE(overlay.is_open());
    EXPECT_EQ(canvas.window, kEditorWindow);
    EXPECT_EQ(canvas.order, editor::OpeningOverlay::kCanvasOrder);
    EXPECT_EQ(canvas.fit, engine::ui::UiFit::Fixed);
    EXPECT_EQ(canvas.rect, engine::render::Rect{});

    overlay.open("Sandbox", "sandbox_game");

    EXPECT_TRUE(overlay.is_open());
    EXPECT_EQ(canvas.fit, engine::ui::UiFit::FillWindow);
    EXPECT_EQ(overlay.view_model()->title.get(), "Opening Sandbox...");
    EXPECT_EQ(overlay.view_model()->stepText.get(), "Building sandbox_game...");
    EXPECT_EQ(overlay.view_model()->detailText.get(), "Starting CMake...");
    EXPECT_FLOAT_EQ(overlay.progress(), 0.10f);
    EXPECT_FLOAT_EQ(overlay.view_model()->progressWidth.get(), 0.10f * editor::OpeningOverlay::kTrackWidth);

    overlay.close();

    EXPECT_FALSE(overlay.is_open());
    EXPECT_EQ(canvas.fit, engine::ui::UiFit::Fixed);
    EXPECT_EQ(canvas.rect, engine::render::Rect{});
}

TEST(OpeningOverlay, ParsesConfigureAndBuildProgressFromOutputLines) {
    editor::OpeningOverlay overlay;
    overlay.open("Sandbox", "sandbox_game");

    overlay.feed_lines(std::vector<std::string>{
            "> cmake -S /proj -B /proj/build-editor",
    });
    EXPECT_EQ(overlay.view_model()->stepText.get(), "Configuring CMake...");
    EXPECT_EQ(overlay.view_model()->detailText.get(), "> cmake -S /proj -B /proj/build-editor");
    EXPECT_FLOAT_EQ(overlay.progress(), 0.15f);

    overlay.feed_lines(std::vector<std::string>{
            "-- Configuring done",
            "   ",
            "-- Generating done",
    });
    EXPECT_EQ(overlay.view_model()->detailText.get(), "-- Generating done");
    EXPECT_GT(overlay.progress(), 0.15f);

    overlay.feed_lines(std::vector<std::string>{
            "> cmake --build /proj/build-editor --config DebugGame --target sandbox_game --parallel",
    });
    EXPECT_EQ(overlay.view_model()->stepText.get(), "Building sandbox_game...");
    EXPECT_FLOAT_EQ(overlay.progress(), 0.35f);

    overlay.feed_lines(std::vector<std::string>{
            "[1/4] Building CXX object a.cpp.obj",
    });
    EXPECT_FLOAT_EQ(overlay.progress(), 0.50f);
    EXPECT_FLOAT_EQ(overlay.view_model()->progressWidth.get(), 0.50f * editor::OpeningOverlay::kTrackWidth);

    overlay.feed_lines(std::vector<std::string>{
            "[ 75%] Building CXX object c.cpp.obj",
    });
    EXPECT_FLOAT_EQ(overlay.progress(), 0.80f);

    overlay.feed_lines(std::vector<std::string>{
            "[4/4] Linking CXX shared library sandbox_game.dll",
    });
    EXPECT_FLOAT_EQ(overlay.progress(), 0.95f);
    EXPECT_EQ(overlay.view_model()->detailText.get(), "[4/4] Linking CXX shared library sandbox_game.dll");
}

TEST(OpeningOverlay, SkipCommandRecordsAndClearsSkipRequest) {
    editor::OpeningOverlay overlay;
    overlay.view_model()->skip.execute();
    EXPECT_FALSE(overlay.take_skip_requested()) << "ignored while closed";

    overlay.open("Sandbox", "sandbox_game");
    EXPECT_TRUE(overlay.view_model()->skip.can_execute());
    overlay.view_model()->skip.execute();
    EXPECT_TRUE(overlay.take_skip_requested());
    EXPECT_FALSE(overlay.take_skip_requested());
}
