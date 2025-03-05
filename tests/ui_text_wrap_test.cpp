#include <gtest/gtest.h>

#include "ui/painter.h"
#include "ui/text_wrap.h"

#include <engine/resources/asset_id.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using engine::ui::TextLine;

// One unit per byte, so a row's width is its byte length.
std::vector<TextLine> wrap_bytes(std::string_view text, float max_width) {
    return engine::ui::break_text_lines(
            text, max_width, [](std::string_view slice) { return static_cast<float>(slice.size()); });
}

std::vector<std::string> rows_of(std::string_view text, const std::vector<TextLine>& lines) {
    std::vector<std::string> rows;
    for (const TextLine& line : lines) {
        rows.emplace_back(text.substr(line.begin, line.end - line.begin));
    }
    return rows;
}

TEST(UiTextWrap, EmptyTextHasNoRows) {
    EXPECT_TRUE(wrap_bytes("", 10.0f).empty());
}

TEST(UiTextWrap, TextThatFitsIsOneRow) {
    const std::string text = "hello world";
    const auto lines = wrap_bytes(text, 11.0f);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].begin, 0u);
    EXPECT_EQ(lines[0].end, 11u);
    EXPECT_FLOAT_EQ(lines[0].width, 11.0f);
}

TEST(UiTextWrap, BreaksAtTheLastWordThatFits) {
    const std::string text = "hello world foo";
    EXPECT_EQ(rows_of(text, wrap_bytes(text, 11.0f)), (std::vector<std::string>{"hello world", "foo"}));
    EXPECT_EQ(rows_of(text, wrap_bytes(text, 5.0f)), (std::vector<std::string>{"hello", "world", "foo"}));
}

TEST(UiTextWrap, RowWidthExcludesTheSpaceItBrokeAt) {
    const auto lines = wrap_bytes("hello world", 7.0f);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_FLOAT_EQ(lines[0].width, 5.0f);
    EXPECT_FLOAT_EQ(lines[1].width, 5.0f);
}

TEST(UiTextWrap, SpacesAtTheStartOfARowAreDropped) {
    const std::string text = "  ab   cd";
    const auto lines = wrap_bytes(text, 2.0f);
    EXPECT_EQ(rows_of(text, lines), (std::vector<std::string>{"ab", "cd"}));
    EXPECT_EQ(lines[0].begin, 2u);
    EXPECT_EQ(lines[1].begin, 7u);
}

TEST(UiTextWrap, WordWiderThanTheRowIsSplitByCharacter) {
    const std::string text = "abcdefghij";
    EXPECT_EQ(rows_of(text, wrap_bytes(text, 4.0f)), (std::vector<std::string>{"abcd", "efgh", "ij"}));
}

TEST(UiTextWrap, LongWordAfterShortOneStartsItsOwnRow) {
    const std::string text = "ab cdefghij";
    EXPECT_EQ(rows_of(text, wrap_bytes(text, 4.0f)), (std::vector<std::string>{"ab", "cdef", "ghij"}));
}

TEST(UiTextWrap, RowsHoldAtLeastOneCharacterEvenIfItIsTooWide) {
    const std::string text = "abc";
    EXPECT_EQ(rows_of(text, wrap_bytes(text, 0.5f)), (std::vector<std::string>{"a", "b", "c"}));
}

TEST(UiTextWrap, NewlineAlwaysBreaksAndBlankLinesStayRows) {
    const std::string text = "a\n\nb";
    const auto lines = wrap_bytes(text, 100.0f);
    EXPECT_EQ(rows_of(text, lines), (std::vector<std::string>{"a", "", "b"}));
}

TEST(UiTextWrap, TrailingNewlineDoesNotAddARow) {
    const std::string text = "a\n";
    EXPECT_EQ(rows_of(text, wrap_bytes(text, 100.0f)), (std::vector<std::string>{"a"}));
}

TEST(UiTextWrap, CarriageReturnBeforeNewlineIsNotPartOfTheRow) {
    const std::string text = "ab\r\ncd";
    const auto lines = wrap_bytes(text, 100.0f);
    EXPECT_EQ(rows_of(text, lines), (std::vector<std::string>{"ab", "cd"}));
    EXPECT_FLOAT_EQ(lines[0].width, 2.0f);
}

TEST(UiTextWrap, SplitNeverCutsInsideAUtf8Character) {
    // Three 2-byte characters; width counts characters (non-continuation bytes), two fit a row.
    const std::string text = "\xC3\xA9\xC3\xA9\xC3\xA9";
    const auto lines = engine::ui::break_text_lines(text, 2.0f, [](std::string_view slice) {
        float count = 0.0f;
        for (const char c : slice) {
            if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
                count += 1.0f;
            }
        }
        return count;
    });
    EXPECT_EQ(rows_of(text, lines), (std::vector<std::string>{"\xC3\xA9\xC3\xA9", "\xC3\xA9"}));
}

class MeasuringPainter : public engine::ui::IUiPainter {
public:
    void save() override {}
    void restore() override {}
    void scissor(const engine::render::Rect&) override {}
    void apply_transform(glm::vec2, float, float) override {}
    void apply_view(glm::vec2, glm::vec2, float) override {}
    void set_opacity(float) override {}
    void fill_rounded_rect(const engine::render::Rect&, float, glm::vec4) override {}
    void fill_rounded_rect_gradient(const engine::render::Rect&, float, const engine::ui::Gradient&) override {}
    void stroke_rounded_rect(const engine::render::Rect&, float, float, glm::vec4) override {}
    void draw_line(glm::vec2, glm::vec2, glm::vec4, float) override {}
    void stroke_arc(glm::vec2, float, float, float, float, glm::vec4) override {}
    void fill_path(std::span<const engine::ui::PathSegment>, glm::vec4) override {}
    void set_font(engine::AssetId, float) override {}
    void fill_text(std::string_view, glm::vec2, glm::vec4, engine::ui::UiAlign, engine::ui::UiAlign) override {}
    void image(engine::AssetId, const engine::render::Rect&) override {}
    void image_repeat(engine::AssetId, const engine::render::Rect&) override {}
    void image_nine_slice(engine::AssetId, const engine::render::Rect&, const engine::ui::BoxInsets&) override {}
    glm::vec2 measure_text(std::string_view text, engine::AssetId, float size) override {
        return {static_cast<float>(text.size()) * size * 0.5f, size};
    }
};

TEST(UiTextWrap, DefaultPainterBreakLinesWrapsThroughMeasureText) {
    MeasuringPainter painter;
    const std::string text = "hello world";
    // 5 per character at size 10, so 30 wide holds six characters.
    const engine::ui::TextBlock block = painter.break_lines(text, engine::AssetId{}, 10.0f, 30.0f);
    EXPECT_FLOAT_EQ(block.line_height, 10.0f);
    EXPECT_EQ(rows_of(text, block.lines), (std::vector<std::string>{"hello", "world"}));
    EXPECT_FLOAT_EQ(block.lines[0].width, 25.0f);
}

class CountingPainter final : public MeasuringPainter {
public:
    int break_calls = 0;

    engine::ui::TextBlock break_lines(
            std::string_view text, engine::AssetId font, float size, float max_width) override {
        ++break_calls;
        return MeasuringPainter::break_lines(text, font, size, max_width);
    }
};

const engine::ui::Element* find_class(const engine::ui::Element& root, std::string_view class_name) {
    if (std::ranges::find(root.classes, class_name) != root.classes.end()) {
        return &root;
    }
    for (const engine::ui::Element& child : root.children) {
        if (const engine::ui::Element* found = find_class(child, class_name)) {
            return found;
        }
    }
    return nullptr;
}

// Parses `xml`, styles it with `css`, lays it out on a `canvas_w` x 400 canvas (through `painter`, or painter-less
// when null) and returns the document. Fonts are 10px, so the measuring painters make a character 5px wide and a
// row 10px tall; "hello world" is 55 wide and wraps into "hello" / "world" (25 wide each) below that.
engine::ui::UiDocument laid_out(
        std::string_view xml, std::string_view css, float canvas_w, engine::ui::IUiPainter* painter) {
    auto parsed = engine::ui::parse_xml(xml);
    EXPECT_TRUE(parsed.has_value());
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(css, warnings);
    EXPECT_TRUE(sheet.has_value());
    engine::ui::apply_layout_style(parsed->root, &*sheet, canvas_w, 400.0f);
    engine::ui::layout(*parsed, engine::render::Rect{0.0f, 0.0f, canvas_w, 400.0f}, painter);
    return std::move(*parsed);
}

constexpr std::string_view kWrapXml =
        R"(<Canvas><Stack class="box"><Label class="t" text="hello world"/></Stack></Canvas>)";

TEST(UiTextWrapLayout, LabelWrapsAtTheParentWidth) {
    MeasuringPainter painter;
    const auto doc = laid_out(kWrapXml, ".box { width: 50; } .t { font-size: 10; }", 400.0f, &painter);
    const engine::ui::Element* label = find_class(doc.root, "t");
    ASSERT_NE(label, nullptr);
    EXPECT_FLOAT_EQ(label->layout_rect.w, 25.0f);
    EXPECT_FLOAT_EQ(label->layout_rect.h, 20.0f);
}

TEST(UiTextWrapLayout, TextThatFitsStaysOneLine) {
    MeasuringPainter painter;
    const auto doc = laid_out(kWrapXml, ".box { width: 55; } .t { font-size: 10; }", 400.0f, &painter);
    const engine::ui::Element* label = find_class(doc.root, "t");
    ASSERT_NE(label, nullptr);
    EXPECT_FLOAT_EQ(label->layout_rect.w, 55.0f);
    EXPECT_FLOAT_EQ(label->layout_rect.h, 10.0f);
}

TEST(UiTextWrapLayout, NowrapKeepsOneLinePastTheParentWidth) {
    MeasuringPainter painter;
    const auto doc =
            laid_out(kWrapXml, ".box { width: 50; } .t { font-size: 10; white-space: nowrap; }", 400.0f, &painter);
    const engine::ui::Element* label = find_class(doc.root, "t");
    ASSERT_NE(label, nullptr);
    EXPECT_FLOAT_EQ(label->layout_rect.w, 55.0f);
    EXPECT_FLOAT_EQ(label->layout_rect.h, 10.0f);
}

TEST(UiTextWrapLayout, ExplicitLabelWidthIsTheWrapWidth) {
    MeasuringPainter painter;
    const auto doc = laid_out(kWrapXml, ".t { font-size: 10; width: 30; }", 400.0f, &painter);
    const engine::ui::Element* label = find_class(doc.root, "t");
    ASSERT_NE(label, nullptr);
    EXPECT_FLOAT_EQ(label->layout_rect.w, 30.0f);
    EXPECT_FLOAT_EQ(label->layout_rect.h, 20.0f);
}

TEST(UiTextWrapLayout, MaxWidthIsTheWrapWidthAndHugsTheWidestRow) {
    MeasuringPainter painter;
    const auto doc = laid_out(kWrapXml, ".t { font-size: 10; max-width: 30; }", 400.0f, &painter);
    const engine::ui::Element* label = find_class(doc.root, "t");
    ASSERT_NE(label, nullptr);
    EXPECT_FLOAT_EQ(label->layout_rect.w, 25.0f);
    EXPECT_FLOAT_EQ(label->layout_rect.h, 20.0f);
}

TEST(UiTextWrapLayout, MinWidthWidensTheWrapWidth) {
    MeasuringPainter painter;
    const auto doc = laid_out(kWrapXml, ".box { width: 50; } .t { font-size: 10; min-width: 60; }", 400.0f, &painter);
    const engine::ui::Element* label = find_class(doc.root, "t");
    ASSERT_NE(label, nullptr);
    EXPECT_FLOAT_EQ(label->layout_rect.w, 60.0f);
    EXPECT_FLOAT_EQ(label->layout_rect.h, 10.0f);
}

TEST(UiTextWrapLayout, PaddingAndMarginComeOffTheWrapWidth) {
    MeasuringPainter painter;
    // 50 - margin 2*3 - padding 2*5 = 34 for text: "hello world" (55) wraps, rows are 25 wide.
    const auto doc =
            laid_out(kWrapXml, ".box { width: 50; } .t { font-size: 10; padding: 5; margin: 3; }", 400.0f, &painter);
    const engine::ui::Element* label = find_class(doc.root, "t");
    ASSERT_NE(label, nullptr);
    EXPECT_FLOAT_EQ(label->layout_rect.w, 35.0f);
    EXPECT_FLOAT_EQ(label->layout_rect.h, 30.0f);
}

TEST(UiTextWrapLayout, NewlineBreaksARowWithNoWidthLimit) {
    MeasuringPainter painter;
    const auto doc = laid_out(
            R"(<Canvas><Label class="t" text="ab&#10;cde"/></Canvas>)", ".t { font-size: 10; }", 400.0f, &painter);
    const engine::ui::Element* label = find_class(doc.root, "t");
    ASSERT_NE(label, nullptr);
    ASSERT_NE(label->text.find('\n'), std::string::npos);
    EXPECT_FLOAT_EQ(label->layout_rect.w, 15.0f);
    EXPECT_FLOAT_EQ(label->layout_rect.h, 20.0f);
}

TEST(UiTextWrapLayout, HugStackInsideASizedStackStillWrapsItsLabel) {
    MeasuringPainter painter;
    const auto doc = laid_out(
            R"(<Canvas><Stack class="outer"><Stack class="inner"><Label class="t" text="hello world"/></Stack></Stack></Canvas>)",
            ".outer { width: 50; } .t { font-size: 10; }", 400.0f, &painter);
    const engine::ui::Element* inner = find_class(doc.root, "inner");
    const engine::ui::Element* label = find_class(doc.root, "t");
    ASSERT_NE(inner, nullptr);
    ASSERT_NE(label, nullptr);
    EXPECT_FLOAT_EQ(inner->layout_rect.w, 25.0f);
    EXPECT_FLOAT_EQ(inner->layout_rect.h, 20.0f);
    EXPECT_FLOAT_EQ(label->layout_rect.h, 20.0f);
}

TEST(UiTextWrapLayout, AbsoluteLabelStretchedByInsetsWrapsAtTheStretchedWidth) {
    MeasuringPainter painter;
    const auto doc = laid_out(R"(<Canvas><Label class="t" text="hello world"/></Canvas>)",
            ".t { font-size: 10; position: absolute; left: 0; right: 350; }", 400.0f, &painter);
    const engine::ui::Element* label = find_class(doc.root, "t");
    ASSERT_NE(label, nullptr);
    EXPECT_FLOAT_EQ(label->layout_rect.w, 50.0f);
    EXPECT_FLOAT_EQ(label->layout_rect.h, 20.0f);
}

TEST(UiTextWrapLayout, PainterLessLayoutWrapsWithTheSameRules) {
    const auto doc = laid_out(kWrapXml, ".box { width: 50; } .t { font-size: 10; }", 400.0f, nullptr);
    const engine::ui::Element* label = find_class(doc.root, "t");
    ASSERT_NE(label, nullptr);
    EXPECT_FLOAT_EQ(label->layout_rect.w, 25.0f);
    EXPECT_FLOAT_EQ(label->layout_rect.h, 20.0f);
}

TEST(UiTextWrapLayout, WrappedRowsAreMemoizedPerWidth) {
    CountingPainter painter;
    auto parsed = engine::ui::parse_xml(kWrapXml);
    ASSERT_TRUE(parsed.has_value());
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(".box { width: 50; } .t { font-size: 10; }", warnings);
    ASSERT_TRUE(sheet.has_value());
    const engine::render::Rect canvas{0.0f, 0.0f, 400.0f, 400.0f};
    engine::ui::apply_layout_style(parsed->root, &*sheet, 400.0f, 400.0f);

    engine::ui::layout(*parsed, canvas, &painter);
    EXPECT_EQ(painter.break_calls, 1);
    engine::ui::layout(*parsed, canvas, &painter);
    EXPECT_EQ(painter.break_calls, 1);

    engine::ui::Element& box = parsed->root.children.front();
    box.children.front().text = "hello brave new world";
    engine::ui::layout(*parsed, canvas, &painter);
    EXPECT_EQ(painter.break_calls, 2);
    engine::ui::layout(*parsed, canvas, &painter);
    EXPECT_EQ(painter.break_calls, 2);
}

}
