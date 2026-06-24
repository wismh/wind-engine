#include <gtest/gtest.h>

#include "ui/painter.h"

#include <engine/render/commands.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <string>
#include <string_view>
#include <vector>

namespace {

engine::ui::Stylesheet must_parse_css(std::string_view css) {
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(css, warnings);
    EXPECT_TRUE(sheet.has_value());
    return sheet.value_or(engine::ui::Stylesheet{});
}

void lay_out(engine::ui::UiDocument &document, const engine::ui::Stylesheet &sheet) {
    engine::ui::apply_layout_style(document.root, &sheet, 800.0f, 600.0f);
    engine::ui::layout(document, engine::render::Rect{0.0f, 0.0f, 800.0f, 600.0f});
}

engine::ui::Element &must_id(engine::ui::UiDocument &document, std::string_view id) {
    engine::ui::Element *found = engine::ui::find_by_id(document.root, id);
    if (found == nullptr) {
        ADD_FAILURE() << id;
        static engine::ui::Element missing;
        return missing;
    }
    return *found;
}

} // namespace

TEST(UiFlexGrow, SplitsFreeSpaceByWeight) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="row">
            <Label id="a" text="a"/>
            <Label id="b" text="b"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        #row { flex-direction: row; width: 300; height: 20; }
        #a { width: 40; height: 20; flex-grow: 1; }
        #b { width: 40; height: 20; flex-grow: 1; }
    )");
    lay_out(*parsed, sheet);
    EXPECT_FLOAT_EQ(must_id(*parsed, "a").layout_rect.w, 150.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "b").layout_rect.w, 150.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "b").layout_rect.x, 150.0f);
}

TEST(UiFlexGrow, ZeroTakesNothing) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="row">
            <Label id="a" text="a"/>
            <Label id="b" text="b"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        #row { flex-direction: row; width: 300; height: 20; }
        #a { width: 40; height: 20; flex-grow: 1; }
        #b { width: 40; height: 20; }
    )");
    lay_out(*parsed, sheet);
    EXPECT_FLOAT_EQ(must_id(*parsed, "a").layout_rect.w, 260.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "b").layout_rect.w, 40.0f);
}

TEST(UiFlexGrow, MaxWidthGivesTheRestToTheSibling) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="row">
            <Label id="a" text="a"/>
            <Label id="b" text="b"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        #row { flex-direction: row; width: 300; height: 20; }
        #a { width: 40; height: 20; flex-grow: 1; max-width: 100; }
        #b { width: 40; height: 20; flex-grow: 1; }
    )");
    lay_out(*parsed, sheet);
    EXPECT_FLOAT_EQ(must_id(*parsed, "a").layout_rect.w, 100.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "b").layout_rect.w, 200.0f);
}

TEST(UiFlexGrow, JustifyCentersWhatGrowthDoesNotTake) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="row">
            <Label id="a" text="a"/>
            <Label id="b" text="b"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        #row { flex-direction: row; width: 300; height: 20; justify-content: center; }
        #a { width: 40; height: 20; }
        #b { width: 40; height: 20; }
    )");
    lay_out(*parsed, sheet);
    EXPECT_FLOAT_EQ(must_id(*parsed, "a").layout_rect.x, 110.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "b").layout_rect.x, 150.0f);
}

TEST(UiFlexGrow, HugStackHasNoFreeSpace) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="row">
            <Label id="a" text="a"/>
            <Label id="b" text="b"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        #row { flex-direction: row; height: 20; }
        #a { width: 40; height: 20; flex-grow: 1; }
        #b { width: 40; height: 20; flex-grow: 1; }
    )");
    lay_out(*parsed, sheet);
    EXPECT_FLOAT_EQ(must_id(*parsed, "a").layout_rect.w, 40.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "b").layout_rect.w, 40.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "row").layout_rect.w, 80.0f);
}

TEST(UiFlexGrow, VerticalStackGrowsHeight) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="col">
            <Label id="a" text="a"/>
            <Label id="b" text="b"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        #col { width: 40; height: 200; }
        #a { width: 40; height: 30; flex-grow: 1; }
        #b { width: 40; height: 30; flex-grow: 1; }
    )");
    lay_out(*parsed, sheet);
    EXPECT_FLOAT_EQ(must_id(*parsed, "a").layout_rect.h, 100.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "b").layout_rect.h, 100.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "b").layout_rect.y, 100.0f);
}

TEST(UiFlexGrow, InvalidValueStaysZero) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="row">
            <Label id="a" text="a"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        #row { flex-direction: row; width: 300; height: 20; }
        #a { width: 40; height: 20; flex-grow: 1px; }
    )");
    lay_out(*parsed, sheet);
    EXPECT_FLOAT_EQ(must_id(*parsed, "a").flex_grow, 0.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "a").layout_rect.w, 40.0f);
}

TEST(UiFlexGrow, AbsoluteChildDoesNotGrow) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="row">
            <Label id="a" text="a"/>
            <Label id="abs" text="x"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        #row { flex-direction: row; width: 300; height: 40; }
        #a { width: 40; height: 20; flex-grow: 1; }
        #abs { position: absolute; width: 10; height: 10; flex-grow: 1; }
    )");
    lay_out(*parsed, sheet);
    EXPECT_FLOAT_EQ(must_id(*parsed, "a").layout_rect.w, 300.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "abs").layout_rect.w, 10.0f);
}
