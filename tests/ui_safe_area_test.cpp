#include <gtest/gtest.h>

#include "ui/painter.h"

#include <engine/ecs/world.h>
#include <engine/render/commands.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/presentation.h>
#include <engine/ui/stylesheet.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// docs/tech/modules/UI.md (Safe area): WindowSize::safe, canvas_safe_insets, and var(--safe-*) from the sheet's
// environment.

namespace {

using engine::ui::SafeInsets;
using engine::ui::UiCanvas;
using engine::ui::UiFit;
using engine::ui::WindowSize;

engine::ui::Stylesheet must_parse_css(std::string_view css) {
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(css, warnings);
    EXPECT_TRUE(sheet.has_value());
    return sheet.value_or(engine::ui::Stylesheet{});
}

// y of the first child inside `.row`, relative to the row: the row's top padding.
float top_padding(const engine::ui::Stylesheet &sheet) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Stack class="row"><Canvas class="mark"/></Stack></Canvas>)");
    EXPECT_TRUE(parsed.has_value());
    engine::ui::apply_layout_style(parsed->root, &sheet, 800.0f, 600.0f);
    engine::ui::layout(*parsed, engine::render::Rect{0.0f, 0.0f, 800.0f, 600.0f});
    const engine::ui::Element &row = parsed->root.children[0];
    return row.children[0].layout_rect.y - row.layout_rect.y;
}

constexpr char kPaddingCss[] = R"(
    .row { width: 100px; height: 100px; padding: var(--safe-top, 5px) 0 0 0; }
    .mark { width: 10px; height: 10px; }
)";

TEST(UiSafeArea, FillWindowCanvasGetsTheWindowInsetsUnchanged) {
    UiCanvas canvas;
    canvas.rect = {0.0f, 0.0f, 800.0f, 600.0f};
    const WindowSize window{800, 600, SafeInsets{10.0f, 30.0f, 20.0f, 40.0f}};

    EXPECT_EQ(engine::ui::canvas_safe_insets(canvas, window), (SafeInsets{10.0f, 30.0f, 20.0f, 40.0f}));
}

TEST(UiSafeArea, FixedCanvasShortOfAnUnsafeEdgeGetsZeroThere) {
    UiCanvas canvas;
    canvas.fit = UiFit::Fixed;
    canvas.rect = {20.0f, 0.0f, 760.0f, 500.0f};
    const WindowSize window{800, 600, SafeInsets{10.0f, 30.0f, 50.0f, 40.0f}};

    const SafeInsets insets = engine::ui::canvas_safe_insets(canvas, window);
    EXPECT_FLOAT_EQ(insets.left, 0.0f) << "the canvas starts 20px in, past the 10px inset";
    EXPECT_FLOAT_EQ(insets.top, 30.0f);
    EXPECT_FLOAT_EQ(insets.right, 30.0f) << "20px gap to the window edge leaves 30 of the 50px inset";
    EXPECT_FLOAT_EQ(insets.bottom, 0.0f) << "100px gap to the window edge, past the 40px inset";
}

TEST(UiSafeArea, ScaledCanvasReportsInsetsInDesignUnits) {
    engine::ecs::World world;
    UiCanvas canvas;
    canvas.fit = UiFit::ScaleWithScreenSize;
    canvas.reference_size = {400.0f, 400.0f};
    engine::ui::presentation_of(world).sizes.sizes[engine::kPrimaryWindow] =
            WindowSize{1000, 500, SafeInsets{100.0f, 50.0f, 100.0f, 25.0f}};
    const engine::ecs::Entity entity = engine::ui::spawn_canvas(world, canvas, engine::ui::UiDocument{});
    engine::ui::apply_canvas_fit(world);

    const UiCanvas &fitted = world.get<UiCanvas>(entity);
    ASSERT_FLOAT_EQ(fitted.rect.x, 250.0f) << "a 500x500 square centered in 1000x500";
    const SafeInsets insets = engine::ui::canvas_safe_insets(
            fitted, engine::ui::window_size_for(world, engine::kPrimaryWindow));
    EXPECT_FLOAT_EQ(insets.left, 0.0f) << "the letterbox bar is wider than the inset";
    EXPECT_FLOAT_EQ(insets.right, 0.0f);
    EXPECT_FLOAT_EQ(insets.top, 40.0f) << "50 window px at scale 1.25";
    EXPECT_FLOAT_EQ(insets.bottom, 20.0f) << "25 window px at scale 1.25";
}

TEST(UiSafeArea, ApplyCanvasFitWritesTheEnvironmentOfEveryCanvasSheet) {
    engine::ecs::World world;
    UiCanvas canvas;
    const engine::ecs::Entity entity =
            engine::ui::spawn_canvas(world, canvas, engine::ui::UiDocument{}, must_parse_css(kPaddingCss));
    engine::ui::presentation_of(world).sizes.sizes[engine::kPrimaryWindow] =
            WindowSize{800, 600, SafeInsets{1.0f, 2.0f, 3.0f, 4.0f}};
    engine::ui::apply_canvas_fit(world);

    const engine::ui::Stylesheet &sheet = *world.get<engine::ui::UiInstance>(entity).stylesheet;
    EXPECT_EQ(sheet.environment.at("safe-left"), "1px");
    EXPECT_EQ(sheet.environment.at("safe-top"), "2px");
    EXPECT_EQ(sheet.environment.at("safe-right"), "3px");
    EXPECT_EQ(sheet.environment.at("safe-bottom"), "4px");
}

TEST(UiSafeArea, SheetTakesANewGenerationOnlyWhenAValueChanges) {
    engine::ui::Stylesheet sheet = must_parse_css(kPaddingCss);
    sheet.set_environment("safe-top", "10px");
    const std::uint64_t first = sheet.generation;

    sheet.set_environment("safe-top", "10px");
    EXPECT_EQ(sheet.generation, first) << "an equal value keeps every cache";

    sheet.set_environment("safe-top", "12px");
    EXPECT_NE(sheet.generation, first);
}

TEST(UiSafeArea, VarReadsTheEnvironmentOnAnyElement) {
    engine::ui::Stylesheet sheet = must_parse_css(kPaddingCss);
    EXPECT_FLOAT_EQ(top_padding(sheet), 5.0f) << "no environment: the fallback";

    sheet.set_environment("safe-top", "30px");
    EXPECT_FLOAT_EQ(top_padding(sheet), 30.0f) << ".row is not the canvas root, and custom properties do not inherit";
}

TEST(UiSafeArea, ACascadedCustomPropertyBeatsTheEnvironment) {
    engine::ui::Stylesheet sheet = must_parse_css(R"(
        .row { --safe-top: 8px; width: 100px; height: 100px; padding: var(--safe-top) 0 0 0; }
        .mark { width: 10px; height: 10px; }
    )");
    sheet.set_environment("safe-top", "30px");

    EXPECT_FLOAT_EQ(top_padding(sheet), 8.0f);
}

}
