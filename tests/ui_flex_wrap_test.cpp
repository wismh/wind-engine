#include <gtest/gtest.h>

#include "ui/painter.h"

#include <engine/render/commands.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <string>
#include <string_view>
#include <vector>

namespace {

engine::ui::Stylesheet must_parse_css(std::string_view css, std::vector<std::string> *warnings = nullptr) {
    std::vector<std::string> local;
    std::vector<std::string> &out = warnings != nullptr ? *warnings : local;
    auto sheet = engine::ui::parse_css(css, out);
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

TEST(UiFlexWrap, ParsesWrapAndLeavesAnUnknownValueAlone) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="plain"/>
          <Stack id="wrap"/>
          <Stack id="reverse"/>
          <Stack id="nowrap"/>
          <Stack id="bad"/>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    std::vector<std::string> warnings;
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        #wrap { flex-wrap: wrap; }
        #reverse { flex-wrap: wrap-reverse; }
        #nowrap { flex-wrap: nowrap; }
        #bad { flex-wrap: sideways; }
    )",
                                                         &warnings);
    EXPECT_TRUE(warnings.empty());
    lay_out(*parsed, sheet);
    EXPECT_EQ(must_id(*parsed, "plain").flex_wrap, engine::ui::FlexWrap::NoWrap);
    EXPECT_EQ(must_id(*parsed, "wrap").flex_wrap, engine::ui::FlexWrap::Wrap);
    EXPECT_EQ(must_id(*parsed, "reverse").flex_wrap, engine::ui::FlexWrap::WrapReverse);
    EXPECT_EQ(must_id(*parsed, "nowrap").flex_wrap, engine::ui::FlexWrap::NoWrap);
    EXPECT_EQ(must_id(*parsed, "bad").flex_wrap, engine::ui::FlexWrap::NoWrap);
}

TEST(UiFlexWrap, WrapsCardsOntoTheNextRow) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="grid">
            <Label id="a" class="card" text="a"/>
            <Label id="b" class="card" text="b"/>
            <Label id="c" class="card" text="c"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        #grid { flex-direction: row; flex-wrap: wrap; width: 250; gap: 10; }
        .card { width: 100; height: 40; }
    )");
    lay_out(*parsed, sheet);
    const engine::ui::Element &grid = must_id(*parsed, "grid");
    EXPECT_FLOAT_EQ(grid.layout_rect.w, 250.0f);
    EXPECT_FLOAT_EQ(grid.layout_rect.h, 90.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "a").layout_rect.x, 0.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "a").layout_rect.y, 0.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "b").layout_rect.x, 110.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "b").layout_rect.y, 0.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "c").layout_rect.x, 0.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "c").layout_rect.y, 50.0f);
    EXPECT_FLOAT_EQ(grid.max_scroll_y, 0.0f);
}

TEST(UiFlexWrap, StaysOneRowWhenTheCardsFit) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="grid">
            <Label id="a" class="card" text="a"/>
            <Label id="b" class="card" text="b"/>
            <Label id="c" class="card" text="c"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        #grid { flex-direction: row; flex-wrap: wrap; width: 400; gap: 10; }
        .card { width: 100; height: 40; }
    )");
    lay_out(*parsed, sheet);
    EXPECT_FLOAT_EQ(must_id(*parsed, "grid").layout_rect.h, 40.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "c").layout_rect.x, 220.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "c").layout_rect.y, 0.0f);
}

TEST(UiFlexWrap, NowrapKeepsOneRowAndScrolls) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="grid">
            <Label id="a" class="card" text="a"/>
            <Label id="b" class="card" text="b"/>
            <Label id="c" class="card" text="c"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        #grid { flex-direction: row; width: 250; gap: 10; }
        .card { width: 100; height: 40; }
    )");
    lay_out(*parsed, sheet);
    EXPECT_FLOAT_EQ(must_id(*parsed, "c").layout_rect.x, 220.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "c").layout_rect.y, 0.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "grid").layout_rect.h, 40.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "grid").max_scroll_x, 70.0f);
}

TEST(UiFlexWrap, FillsThePanelAndWrapsAsItNarrows) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="panel">
            <Stack id="grid">
              <Label id="a" class="card" text="a"/>
              <Label id="b" class="card" text="b"/>
              <Label id="c" class="card" text="c"/>
            </Stack>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        #panel { width: 250; flex-direction: column; }
        #grid { flex-direction: row; flex-wrap: wrap; width: 100%; gap: 10; }
        .card { width: 100; height: 40; }
    )");
    lay_out(*parsed, sheet);
    const engine::ui::Element &grid = must_id(*parsed, "grid");
    EXPECT_FLOAT_EQ(grid.layout_rect.w, 250.0f);
    EXPECT_FLOAT_EQ(grid.layout_rect.h, 90.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "c").layout_rect.y, grid.layout_rect.y + 50.0f);
}

TEST(UiFlexWrap, HugsTheLongestLineInsideANarrowParent) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="panel">
            <Stack id="grid">
              <Label id="a" class="card" text="a"/>
              <Label id="b" class="card" text="b"/>
              <Label id="c" class="card" text="c"/>
            </Stack>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        #panel { width: 250; flex-direction: column; }
        #grid { flex-direction: row; flex-wrap: wrap; gap: 10; }
        .card { width: 100; height: 40; }
    )");
    lay_out(*parsed, sheet);
    EXPECT_FLOAT_EQ(must_id(*parsed, "grid").layout_rect.w, 210.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "grid").layout_rect.h, 90.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "c").layout_rect.x, must_id(*parsed, "grid").layout_rect.x);
}

TEST(UiFlexWrap, MaxWidthIsEnoughToWrapAgainst) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="grid">
            <Label id="a" class="card" text="a"/>
            <Label id="b" class="card" text="b"/>
            <Label id="c" class="card" text="c"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        #grid { flex-direction: row; flex-wrap: wrap; max-width: 250; gap: 10; }
        .card { width: 100; height: 40; }
    )");
    lay_out(*parsed, sheet);
    EXPECT_FLOAT_EQ(must_id(*parsed, "grid").layout_rect.w, 210.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "grid").layout_rect.h, 90.0f);
}

TEST(UiFlexWrap, WrapReversePutsTheFirstLineAtTheEnd) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="grid">
            <Label id="a" class="card" text="a"/>
            <Label id="b" class="card" text="b"/>
            <Label id="c" class="card" text="c"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        #grid { flex-direction: row; flex-wrap: wrap-reverse; width: 250; height: 100; gap: 10; }
        .card { width: 100; height: 40; }
    )");
    lay_out(*parsed, sheet);
    EXPECT_FLOAT_EQ(must_id(*parsed, "a").layout_rect.y, 60.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "b").layout_rect.y, 60.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "c").layout_rect.y, 10.0f);
}

TEST(UiFlexWrap, FlexGrowAndJustifyRunPerLine) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="grid">
            <Label id="a" class="card" text="a"/>
            <Label id="b" class="card" text="b"/>
            <Label id="c" class="card" text="c"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        #grid { flex-direction: row; flex-wrap: wrap; width: 200; gap: 0; }
        .card { width: 80; height: 20; flex-grow: 1; }
    )");
    lay_out(*parsed, sheet);
    EXPECT_FLOAT_EQ(must_id(*parsed, "a").layout_rect.w, 100.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "b").layout_rect.x, 100.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "b").layout_rect.w, 100.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "c").layout_rect.y, 20.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "c").layout_rect.w, 200.0f);

    auto justified = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="grid">
            <Label id="a" class="card" text="a"/>
            <Label id="b" class="card" text="b"/>
            <Label id="c" class="card" text="c"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(justified.has_value());
    const engine::ui::Stylesheet justify_sheet = must_parse_css(R"(
        #grid { flex-direction: row; flex-wrap: wrap; justify-content: center; width: 300; gap: 0; }
        .card { width: 120; height: 40; }
    )");
    lay_out(*justified, justify_sheet);
    EXPECT_FLOAT_EQ(must_id(*justified, "a").layout_rect.x, 30.0f);
    EXPECT_FLOAT_EQ(must_id(*justified, "b").layout_rect.x, 150.0f);
    EXPECT_FLOAT_EQ(must_id(*justified, "c").layout_rect.x, 90.0f);
    EXPECT_FLOAT_EQ(must_id(*justified, "c").layout_rect.y, 40.0f);
}

TEST(UiFlexWrap, AlignItemsCentersInsideTheLine) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="grid">
            <Label id="a" class="card" text="a"/>
            <Label id="b" class="card" text="b"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        #grid { flex-direction: row; flex-wrap: wrap; align-items: center; width: 250; gap: 10; }
        #a { width: 100; height: 20; }
        #b { width: 100; height: 40; }
    )");
    lay_out(*parsed, sheet);
    EXPECT_FLOAT_EQ(must_id(*parsed, "a").layout_rect.y, 10.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "b").layout_rect.y, 0.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "grid").layout_rect.h, 40.0f);
}

TEST(UiFlexWrap, ColumnWrapsOntoTheNextColumn) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="col">
            <Label id="a" class="card" text="a"/>
            <Label id="b" class="card" text="b"/>
            <Label id="c" class="card" text="c"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        #col { flex-direction: column; flex-wrap: wrap; height: 100; gap: 10; }
        .card { width: 30; height: 40; }
    )");
    lay_out(*parsed, sheet);
    EXPECT_FLOAT_EQ(must_id(*parsed, "col").layout_rect.w, 70.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "col").layout_rect.h, 100.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "a").layout_rect.x, 0.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "a").layout_rect.y, 0.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "b").layout_rect.x, 0.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "b").layout_rect.y, 50.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "c").layout_rect.x, 40.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "c").layout_rect.y, 0.0f);
}

TEST(UiFlexWrap, AnItemWiderThanTheLineStaysWhole) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack id="grid">
            <Label id="a" class="card" text="a"/>
            <Label id="b" class="card" text="b"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        #grid { flex-direction: row; flex-wrap: wrap; width: 80; height: 100; gap: 0; }
        .card { width: 100; height: 40; }
    )");
    lay_out(*parsed, sheet);
    EXPECT_FLOAT_EQ(must_id(*parsed, "a").layout_rect.y, 0.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "a").layout_rect.w, 100.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "b").layout_rect.x, 0.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "b").layout_rect.y, 40.0f);
    EXPECT_FLOAT_EQ(must_id(*parsed, "grid").max_scroll_x, 20.0f);
}
