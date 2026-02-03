#include <gtest/gtest.h>

#include "ui/inline_math.h"
#include "ui/math/math_font.h"
#include "ui/math/math_layout.h"
#include "ui/math/math_parser.h"
#include "ui/painter.h"
#include "ui/ui_refs.h"

#include <engine/builtin_ids.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef ENGINE_BUILTIN_ASSETS_DIR
#error "ENGINE_BUILTIN_ASSETS_DIR must be set to the builtin_assets path"
#endif

namespace {

using engine::ui::InlineBox;
using engine::ui::InlineLayout;
using engine::ui::InlineLine;
using engine::ui::InlinePiece;
using engine::ui::InlineSegment;
using engine::ui::InlineSplit;
using engine::ui::TextFontMetrics;

InlinePiece text_piece(std::string text) {
    InlinePiece piece;
    piece.kind = InlinePiece::Kind::Text;
    piece.text = std::move(text);
    return piece;
}

InlinePiece math_piece(std::string source) {
    InlinePiece piece;
    piece.kind = InlinePiece::Kind::Math;
    piece.text = std::move(source);
    return piece;
}

InlineLayout lay(const std::vector<InlinePiece>& pieces, float max_width, bool wrap = true, float strut = 14.0f,
                 InlineBox math = InlineBox{30.0f, 8.0f, 2.0f},
                 TextFontMetrics font = TextFontMetrics{10.0f, 4.0f, 14.0f}) {
    return engine::ui::layout_inline(
            pieces, max_width, wrap, strut, font,
            [](std::size_t, std::size_t begin, std::size_t end) { return static_cast<float>(end - begin); },
            [math](std::size_t) { return math; });
}

std::string segment_text(const std::vector<InlinePiece>& pieces, const InlineSegment& seg) {
    const InlinePiece& piece = pieces[seg.piece];
    if (piece.kind == InlinePiece::Kind::Math) {
        return "[m]";
    }
    return piece.text.substr(seg.begin, seg.end - seg.begin);
}

std::vector<std::string> line_texts(const std::vector<InlinePiece>& pieces, const InlineLayout& layout) {
    std::vector<std::string> rows;
    for (const InlineLine& line : layout.lines) {
        std::string row;
        for (const InlineSegment& seg : line.segments) {
            row += segment_text(pieces, seg);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

const engine::ui::math::MathFont& stix() {
    static const engine::ui::math::MathFont font = [] {
        const std::filesystem::path path = std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR} / "fonts" / "math.otf";
        std::ifstream in(path, std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto loaded = engine::ui::math::MathFont::load(bytes);
        EXPECT_TRUE(loaded.has_value());
        return std::move(*loaded);
    }();
    return font;
}

struct TextCall {
    std::string text;
    glm::vec2 pos{};
    glm::vec4 color{};
};

struct PathCall {
    std::vector<engine::ui::PathSegment> segments;
    glm::vec4 color{};
};

class RecordingPainter : public engine::ui::IUiPainter {
public:
    std::vector<TextCall> texts;
    std::vector<PathCall> paths;
    std::vector<engine::render::Rect> rects;

    [[nodiscard]] const engine::ui::math::MathFont* math_font() const override { return &stix(); }

    void save() override {}
    void restore() override {}
    void scissor(const engine::render::Rect&) override {}
    void apply_transform(glm::vec2, float, float) override {}
    void apply_view(glm::vec2, glm::vec2, float) override {}
    void set_opacity(float) override {}
    void fill_rounded_rect(const engine::render::Rect& rect, float, glm::vec4) override { rects.push_back(rect); }
    void fill_rounded_rect_gradient(const engine::render::Rect&, float, const engine::ui::Gradient&) override {}
    void stroke_rounded_rect(const engine::render::Rect&, float, float, glm::vec4) override {}
    void draw_line(glm::vec2, glm::vec2, glm::vec4, float) override {}
    void stroke_arc(glm::vec2, float, float, float, float, glm::vec4) override {}
    void fill_path(std::span<const engine::ui::PathSegment> path, glm::vec4 color) override {
        paths.push_back({std::vector<engine::ui::PathSegment>(path.begin(), path.end()), color});
    }
    void set_font(engine::AssetId, float) override {}
    void fill_text(std::string_view text, glm::vec2 pos, glm::vec4 color, engine::ui::UiAlign,
                   engine::ui::UiAlign) override {
        texts.push_back({std::string(text), pos, color});
    }
    void image(engine::AssetId, const engine::render::Rect&) override {}
    void image_repeat(engine::AssetId, const engine::render::Rect&) override {}
    void image_nine_slice(engine::AssetId, const engine::render::Rect&, const engine::ui::BoxInsets&) override {}
    glm::vec2 measure_text(std::string_view text, engine::AssetId, float size) override {
        return {static_cast<float>(text.size()) * size * 0.5f, size};
    }
};

engine::ui::Stylesheet must_parse_css(std::string_view css) {
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(css, warnings);
    EXPECT_TRUE(sheet.has_value());
    return *sheet;
}

engine::ui::UiDocument must_parse(std::string_view xml) {
    auto parsed = engine::ui::parse_xml(xml);
    EXPECT_TRUE(parsed.has_value());
    return std::move(*parsed);
}

const engine::ui::Element& only_child(const engine::ui::UiDocument& document) {
    EXPECT_EQ(document.root.children.size(), 1u);
    return document.root.children[0];
}

} // namespace

TEST(InlineSplit, PlainTextIsOnePiece) {
    const InlineSplit split = engine::ui::split_inline("hello");
    ASSERT_EQ(split.pieces.size(), 1u);
    EXPECT_EQ(split.pieces[0].kind, InlinePiece::Kind::Text);
    EXPECT_EQ(split.pieces[0].text, "hello");
    EXPECT_FALSE(split.unclosed);
    EXPECT_FALSE(engine::ui::text_has_inline_markup("hello"));
}

TEST(InlineSplit, OneFormulaBetweenText) {
    const InlineSplit split = engine::ui::split_inline(R"(value \(x\) end)");
    ASSERT_EQ(split.pieces.size(), 3u);
    EXPECT_EQ(split.pieces[0].text, "value ");
    EXPECT_EQ(split.pieces[1].kind, InlinePiece::Kind::Math);
    EXPECT_EQ(split.pieces[1].text, "x");
    EXPECT_EQ(split.pieces[2].text, " end");
    EXPECT_FALSE(split.unclosed);
}

TEST(InlineSplit, FormulaAtEitherEndAndTwoFormulas) {
    const InlineSplit leading = engine::ui::split_inline(R"(\(a\) end)");
    ASSERT_EQ(leading.pieces.size(), 2u);
    EXPECT_EQ(leading.pieces[0].kind, InlinePiece::Kind::Math);
    EXPECT_EQ(leading.pieces[0].text, "a");
    EXPECT_EQ(leading.pieces[1].text, " end");

    const InlineSplit trailing = engine::ui::split_inline(R"(start \(a\))");
    ASSERT_EQ(trailing.pieces.size(), 2u);
    EXPECT_EQ(trailing.pieces[0].text, "start ");
    EXPECT_EQ(trailing.pieces[1].text, "a");

    const InlineSplit two = engine::ui::split_inline(R"(\(a\) and \(b\))");
    ASSERT_EQ(two.pieces.size(), 3u);
    EXPECT_EQ(two.pieces[0].text, "a");
    EXPECT_EQ(two.pieces[1].text, " and ");
    EXPECT_EQ(two.pieces[2].text, "b");
}

TEST(InlineSplit, EmptyFormulaIsAMathPiece) {
    const InlineSplit split = engine::ui::split_inline(R"(\(\))");
    ASSERT_EQ(split.pieces.size(), 1u);
    EXPECT_EQ(split.pieces[0].kind, InlinePiece::Kind::Math);
    EXPECT_TRUE(split.pieces[0].text.empty());
    EXPECT_FALSE(split.unclosed);
}

TEST(InlineSplit, EscapesAreLiteralAndDoNotOpen) {
    const InlineSplit open = engine::ui::split_inline(R"(a\\(b)");
    ASSERT_EQ(open.pieces.size(), 1u);
    EXPECT_EQ(open.pieces[0].kind, InlinePiece::Kind::Text);
    EXPECT_EQ(open.pieces[0].text, R"(a\(b)");
    EXPECT_FALSE(open.unclosed);

    const InlineSplit close = engine::ui::split_inline(R"(a\\))");
    ASSERT_EQ(close.pieces.size(), 1u);
    EXPECT_EQ(close.pieces[0].text, R"(a\))");

    const InlineSplit mixed = engine::ui::split_inline(R"(a\\( \(x\))");
    ASSERT_EQ(mixed.pieces.size(), 2u);
    EXPECT_EQ(mixed.pieces[0].text, R"(a\( )");
    EXPECT_EQ(mixed.pieces[1].kind, InlinePiece::Kind::Math);
    EXPECT_EQ(mixed.pieces[1].text, "x");
}

TEST(InlineSplit, EscapedCloserInsideAFormulaStaysInTheSource) {
    const InlineSplit split = engine::ui::split_inline(R"(\(a\\)b\))");
    ASSERT_EQ(split.pieces.size(), 1u);
    EXPECT_EQ(split.pieces[0].kind, InlinePiece::Kind::Math);
    EXPECT_EQ(split.pieces[0].text, R"(a\)b)");
    EXPECT_FALSE(split.unclosed);
}

TEST(InlineSplit, SourceRangesCoverFormulasEscapesAndUnclosedText) {
    const InlineSplit formula = engine::ui::split_inline(R"(a\(x\))");
    ASSERT_EQ(formula.pieces.size(), 2u);
    EXPECT_EQ(formula.pieces[0].source_begin, 0u);
    EXPECT_EQ(formula.pieces[0].source_end, 1u);
    EXPECT_TRUE(formula.pieces[0].source_of_drawn.empty());
    EXPECT_EQ(formula.pieces[1].kind, InlinePiece::Kind::Math);
    EXPECT_EQ(formula.pieces[1].source_begin, 1u);
    EXPECT_EQ(formula.pieces[1].source_end, std::string(R"(a\(x\))").size());

    const InlineSplit empty = engine::ui::split_inline(R"(\(\))");
    ASSERT_EQ(empty.pieces.size(), 1u);
    EXPECT_EQ(empty.pieces[0].source_begin, 0u);
    EXPECT_EQ(empty.pieces[0].source_end, 4u);

    const InlineSplit escaped = engine::ui::split_inline(R"(a\\(b)");
    ASSERT_EQ(escaped.pieces.size(), 1u);
    EXPECT_EQ(escaped.pieces[0].text, R"(a\(b)");
    EXPECT_EQ(escaped.pieces[0].source_begin, 0u);
    EXPECT_EQ(escaped.pieces[0].source_end, std::string(R"(a\\(b)").size());
    ASSERT_EQ(escaped.pieces[0].source_of_drawn.size(), escaped.pieces[0].text.size() + 1);
    EXPECT_EQ(escaped.pieces[0].source_of_drawn[0], 0u);
    EXPECT_EQ(escaped.pieces[0].source_of_drawn[1], 1u);
    EXPECT_EQ(escaped.pieces[0].source_of_drawn[2], 3u);
    EXPECT_EQ(escaped.pieces[0].source_of_drawn[3], 4u);
    EXPECT_EQ(escaped.pieces[0].source_of_drawn[4], 5u);

    const std::string unclosed_src = R"(value \(x)";
    const InlineSplit unclosed = engine::ui::split_inline(unclosed_src);
    ASSERT_EQ(unclosed.pieces.size(), 1u);
    EXPECT_TRUE(unclosed.unclosed);
    EXPECT_EQ(unclosed.pieces[0].kind, InlinePiece::Kind::Text);
    EXPECT_EQ(unclosed.pieces[0].source_begin, 0u);
    EXPECT_EQ(unclosed.pieces[0].source_end, unclosed_src.size());
    EXPECT_TRUE(unclosed.pieces[0].source_of_drawn.empty());
}

TEST(InlineSplit, UnclosedDelimiterStaysPlainText) {
    const InlineSplit split = engine::ui::split_inline(R"(value \(x)");
    EXPECT_TRUE(split.unclosed);
    ASSERT_EQ(split.pieces.size(), 1u);
    EXPECT_EQ(split.pieces[0].kind, InlinePiece::Kind::Text);
    EXPECT_EQ(split.pieces[0].text, R"(value \(x)");
}

TEST(InlineSplit, NewlineInsideAFormulaStaysInTheFormula) {
    const InlineSplit split = engine::ui::split_inline("\\(a\nb\\)");
    ASSERT_EQ(split.pieces.size(), 1u);
    EXPECT_EQ(split.pieces[0].kind, InlinePiece::Kind::Math);
    EXPECT_EQ(split.pieces[0].text, "a\nb");
}

TEST(InlineSplit, NewlineOutsideAFormulaStaysInTheText) {
    const InlineSplit split = engine::ui::split_inline("a\n\\(x\\)");
    ASSERT_EQ(split.pieces.size(), 2u);
    EXPECT_EQ(split.pieces[0].text, "a\n");
    EXPECT_EQ(split.pieces[1].text, "x");
}

TEST(InlineLayout, WordsOnOneLineStayOneSegment) {
    const std::vector<InlinePiece> pieces{text_piece("hello world")};
    const InlineLayout layout = lay(pieces, 100.0f);
    ASSERT_EQ(layout.lines.size(), 1u);
    ASSERT_EQ(layout.lines[0].segments.size(), 1u);
    EXPECT_EQ(segment_text(pieces, layout.lines[0].segments[0]), "hello world");
    EXPECT_FLOAT_EQ(layout.lines[0].width, 11.0f);
}

TEST(InlineLayout, WrapsAtTheLastWordAndDropsTheBreakSpace) {
    const std::vector<InlinePiece> pieces{text_piece("hello world")};
    const InlineLayout layout = lay(pieces, 7.0f);
    EXPECT_EQ(line_texts(pieces, layout), (std::vector<std::string>{"hello", "world"}));
    EXPECT_FLOAT_EQ(layout.lines[0].width, 5.0f);
    EXPECT_FLOAT_EQ(layout.width, 5.0f);
}

TEST(InlineLayout, DropsSpacesAtTheStartOfARow) {
    const std::vector<InlinePiece> pieces{text_piece("  ab")};
    const InlineLayout layout = lay(pieces, 10.0f);
    ASSERT_EQ(layout.lines.size(), 1u);
    EXPECT_EQ(segment_text(pieces, layout.lines[0].segments[0]), "ab");
    EXPECT_EQ(layout.lines[0].segments[0].begin, 2u);
}

TEST(InlineLayout, SplitsALongWordByUtf8Character) {
    const std::vector<InlinePiece> pieces{text_piece("\xC3\xA9\xC3\xA9")};
    const InlineLayout layout = lay(pieces, 2.0f);
    ASSERT_EQ(layout.lines.size(), 2u);
    EXPECT_EQ(layout.lines[0].segments[0].end - layout.lines[0].segments[0].begin, 2u);
    EXPECT_EQ(layout.lines[1].segments[0].end - layout.lines[1].segments[0].begin, 2u);
}

TEST(InlineLayout, FormulaIsAtomicAndMovesWholeToTheNextLine) {
    const std::vector<InlinePiece> pieces{text_piece("ab"), math_piece("x")};
    const InlineLayout wrapped = lay(pieces, 20.0f, true, 14.0f, InlineBox{30.0f, 8.0f, 2.0f});
    ASSERT_EQ(wrapped.lines.size(), 2u);
    EXPECT_EQ(line_texts(pieces, wrapped), (std::vector<std::string>{"ab", "[m]"}));
    EXPECT_FLOAT_EQ(wrapped.lines[1].width, 30.0f);

    const InlineLayout alone =
            lay(std::vector<InlinePiece>{math_piece("x")}, 10.0f, true, 14.0f, InlineBox{100.0f, 8.0f, 2.0f});
    ASSERT_EQ(alone.lines.size(), 1u);
    ASSERT_EQ(alone.lines[0].segments.size(), 1u);
    EXPECT_FLOAT_EQ(alone.lines[0].width, 100.0f);
    EXPECT_FLOAT_EQ(alone.width, 100.0f);
}

TEST(InlineLayout, AdjacentFormulasBreakBetweenThem) {
    const std::vector<InlinePiece> pieces{math_piece("a"), math_piece("b")};
    const InlineLayout layout = lay(pieces, 50.0f, true, 14.0f, InlineBox{30.0f, 8.0f, 2.0f});
    ASSERT_EQ(layout.lines.size(), 2u);
    EXPECT_FLOAT_EQ(layout.lines[0].segments[0].x, 0.0f);
    EXPECT_FLOAT_EQ(layout.lines[1].width, 30.0f);

    const InlineLayout fit = lay(pieces, 100.0f, true, 14.0f, InlineBox{30.0f, 8.0f, 2.0f});
    ASSERT_EQ(fit.lines.size(), 1u);
    ASSERT_EQ(fit.lines[0].segments.size(), 2u);
    EXPECT_FLOAT_EQ(fit.lines[0].segments[1].x, 30.0f);
}

TEST(InlineLayout, SpaceBeforeAFormulaStaysOnTheTextWhenTheyFit) {
    const std::vector<InlinePiece> pieces{text_piece("hello "), math_piece("x")};
    const InlineLayout layout = lay(pieces, 100.0f, true, 14.0f, InlineBox{10.0f, 8.0f, 2.0f});
    ASSERT_EQ(layout.lines.size(), 1u);
    EXPECT_EQ(line_texts(pieces, layout), (std::vector<std::string>{"hello [m]"}));
    EXPECT_FLOAT_EQ(layout.lines[0].segments.back().x, 6.0f);
}

TEST(InlineLayout, NewlineBreaksAndATrailingNewlineDoesNotAddARow) {
    const std::vector<InlinePiece> pieces{text_piece("a\n\nb")};
    const InlineLayout layout = lay(pieces, 100.0f);
    ASSERT_EQ(layout.lines.size(), 3u);
    EXPECT_EQ(line_texts(pieces, layout), (std::vector<std::string>{"a", "", "b"}));
    EXPECT_FLOAT_EQ(layout.lines[1].width, 0.0f);

    const InlineLayout trailing = lay(std::vector<InlinePiece>{text_piece("a\n")}, 100.0f);
    ASSERT_EQ(trailing.lines.size(), 1u);
    EXPECT_EQ(line_texts(std::vector<InlinePiece>{text_piece("a\n")}, trailing), (std::vector<std::string>{"a"}));
}

TEST(InlineLayout, NewlineInsideAFormulaDoesNotBreakTheLine) {
    const std::vector<InlinePiece> pieces{math_piece("a\nb")};
    const InlineLayout layout = lay(pieces, 100.0f);
    ASSERT_EQ(layout.lines.size(), 1u);
    ASSERT_EQ(layout.lines[0].segments.size(), 1u);
    EXPECT_EQ(pieces[layout.lines[0].segments[0].piece].kind, InlinePiece::Kind::Math);
}

TEST(InlineLayout, NowrapIsOneLineAndSkipsNewlines) {
    const std::vector<InlinePiece> pieces{text_piece("ab\ncd"), math_piece("x")};
    const InlineLayout layout = lay(pieces, 1.0f, false, 14.0f, InlineBox{50.0f, 8.0f, 2.0f});
    ASSERT_EQ(layout.lines.size(), 1u);
    EXPECT_EQ(line_texts(pieces, layout), (std::vector<std::string>{"abcd[m]"}));
    EXPECT_FLOAT_EQ(layout.width, 54.0f);
}

TEST(InlineLayout, TallFormulaGrowsOnlyItsLine) {
    const std::vector<InlinePiece> pieces{text_piece("a\n"), math_piece("x")};
    const InlineLayout layout =
            lay(pieces, 100.0f, true, 20.0f, InlineBox{10.0f, 30.0f, 8.0f}, TextFontMetrics{10.0f, 4.0f, 14.0f});
    ASSERT_EQ(layout.lines.size(), 2u);
    EXPECT_FLOAT_EQ(layout.lines[0].height, 20.0f);
    EXPECT_FLOAT_EQ(layout.lines[0].baseline, 10.0f);
    EXPECT_FLOAT_EQ(layout.lines[1].baseline, 30.0f);
    EXPECT_FLOAT_EQ(layout.lines[1].height, 38.0f);
    EXPECT_FLOAT_EQ(layout.height, 58.0f);
}

TEST(InlineLayout, ShortFormulaSitsOnTheTextBaselineInsideTheStrut) {
    const std::vector<InlinePiece> pieces{text_piece("A"), math_piece("x")};
    const InlineLayout layout =
            lay(pieces, 100.0f, true, 20.0f, InlineBox{5.0f, 3.0f, 1.0f}, TextFontMetrics{10.0f, 4.0f, 14.0f});
    ASSERT_EQ(layout.lines.size(), 1u);
    EXPECT_FLOAT_EQ(layout.lines[0].baseline, 10.0f);
    EXPECT_FLOAT_EQ(layout.lines[0].height, 20.0f);
    EXPECT_FLOAT_EQ(layout.lines[0].segments[1].x, 1.0f);
}

TEST(UiInlineMath, PlainLabelDoesNotRequestTheMathFontOrDrawAFormula) {
    const engine::ui::UiDocument plain = must_parse(R"(<Canvas><Label text="hello"/></Canvas>)");
    const auto fonts = engine::ui::collect_referenced_fonts(plain, nullptr);
    EXPECT_EQ(std::find(fonts.begin(), fonts.end(), engine::builtin::font_math), fonts.end());

    RecordingPainter painter;
    const engine::ui::Stylesheet sheet = must_parse_css("Label { font-size: 20px; }");
    engine::ui::UiDocument document = must_parse(R"(<Canvas><Label text="hello"/></Canvas>)");
    engine::ui::paint_document(document, &sheet, painter,
                               engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 400.f, 200.f}});
    ASSERT_EQ(painter.texts.size(), 1u);
    EXPECT_EQ(painter.texts[0].text, "hello");
    EXPECT_TRUE(painter.paths.empty());
}

TEST(UiInlineMath, LabelAndButtonWithADelimiterRequestTheMathFont) {
    const engine::ui::UiDocument label = must_parse(R"xml(<Canvas><Label text="\(x\)"/></Canvas>)xml");
    const auto label_fonts = engine::ui::collect_referenced_fonts(label, nullptr);
    EXPECT_NE(std::find(label_fonts.begin(), label_fonts.end(), engine::builtin::font_math), label_fonts.end());

    const engine::ui::UiDocument button = must_parse(R"xml(<Canvas><Button content="area \(x\)"/></Canvas>)xml");
    const auto button_fonts = engine::ui::collect_referenced_fonts(button, nullptr);
    EXPECT_NE(std::find(button_fonts.begin(), button_fonts.end(), engine::builtin::font_math), button_fonts.end());

    const engine::ui::UiDocument nested = must_parse(R"xml(<Canvas><Stack><Label text="\(x\)"/></Stack></Canvas>)xml");
    const auto nested_fonts = engine::ui::collect_referenced_fonts(nested, nullptr);
    EXPECT_NE(std::find(nested_fonts.begin(), nested_fonts.end(), engine::builtin::font_math), nested_fonts.end());

    const engine::ui::UiDocument escaped = must_parse(R"xml(<Canvas><Label text="a\\)"/></Canvas>)xml");
    const auto escaped_fonts = engine::ui::collect_referenced_fonts(escaped, nullptr);
    EXPECT_EQ(std::find(escaped_fonts.begin(), escaped_fonts.end(), engine::builtin::font_math), escaped_fonts.end());
}

TEST(UiInlineMath, FractionGrowsTheLineToTheSharedBaseline) {
    constexpr float kSize = 20.0f;
    const engine::ui::math::ParseResult parsed = engine::ui::math::parse_formula(R"(\frac{1}{2})");
    ASSERT_TRUE(parsed.ok());
    const engine::ui::math::MathLayout formula =
            engine::ui::math::layout_formula(parsed.root, stix(), engine::ui::math::LayoutOptions{kSize, false});
    const float ascent = kSize * 0.8f;
    const float descent = kSize * 0.2f;
    const float strut = kSize;
    const float baseline = std::max(ascent, formula.ascent);
    const float line_h = std::max(strut, baseline + std::max(descent, formula.descent));

    const engine::ui::Stylesheet sheet = must_parse_css("Label { font-size: 20px; }");
    RecordingPainter painter;
    engine::ui::UiDocument document = must_parse(R"xml(<Canvas><Label text="A \(\frac{1}{2}\)"/></Canvas>)xml");
    engine::ui::apply_layout_style(document.root, &sheet);
    engine::ui::layout(document, {0.f, 0.f, 800.f, 400.f}, &painter);
    const engine::ui::Element& label = only_child(document);
    EXPECT_NEAR(label.layout_rect.w, 20.0f + formula.width, 0.05f);
    EXPECT_NEAR(label.layout_rect.h, line_h, 0.05f);
    EXPECT_GT(label.layout_rect.h, strut);

    engine::ui::UiDocument plain = must_parse(R"(<Canvas><Label text="A"/></Canvas>)");
    engine::ui::apply_layout_style(plain.root, &sheet);
    engine::ui::layout(plain, {0.f, 0.f, 800.f, 400.f}, &painter);
    EXPECT_GT(label.layout_rect.h, only_child(plain).layout_rect.h);
}

TEST(UiInlineMath, PaintSharesTheBaselineAndTheLabelColor) {
    constexpr float kSize = 20.0f;
    constexpr glm::vec4 kRed{1.0f, 0.0f, 0.0f, 1.0f};
    const engine::ui::math::ParseResult parsed = engine::ui::math::parse_formula(R"(\frac{1}{2})");
    ASSERT_TRUE(parsed.ok());
    const engine::ui::math::MathLayout formula =
            engine::ui::math::layout_formula(parsed.root, stix(), engine::ui::math::LayoutOptions{kSize, false});
    ASSERT_FALSE(formula.rules.empty());
    const float ascent = kSize * 0.8f;
    const float baseline = std::max(ascent, formula.ascent);

    const engine::ui::Stylesheet sheet = must_parse_css("Label { font-size: 20px; color: #ff0000; }");
    RecordingPainter painter;
    engine::ui::UiDocument document = must_parse(R"xml(<Canvas><Label text="A \(\frac{1}{2}\)"/></Canvas>)xml");
    engine::ui::paint_document(document, &sheet, painter,
                               engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 800.f, 400.f},
                                                        .window_width = 800.f,
                                                        .window_height = 400.f});

    ASSERT_EQ(painter.texts.size(), 1u);
    EXPECT_EQ(painter.texts[0].text, "A ");
    EXPECT_EQ(painter.texts[0].color, kRed);
    EXPECT_NEAR(painter.texts[0].pos.y, baseline - ascent, 0.05f);
    EXPECT_NEAR(painter.texts[0].pos.x, 0.0f, 0.05f);

    ASSERT_EQ(painter.paths.size(), 1u);
    EXPECT_EQ(painter.paths[0].color, kRed);
    ASSERT_FALSE(painter.rects.empty());
    const float math_top = baseline - formula.ascent;
    EXPECT_NEAR(painter.rects[0].y, math_top + formula.rules[0].position.y, 0.05f);
    EXPECT_NEAR(painter.rects[0].x, 20.0f + formula.rules[0].position.x, 0.05f);
    EXPECT_NEAR(painter.texts[0].pos.y + ascent, math_top + formula.ascent, 0.05f);
}

TEST(UiInlineMath, TextAlignAndAlignItemsPlaceTheWholeLine) {
    constexpr float kSize = 20.0f;
    const engine::ui::math::ParseResult parsed = engine::ui::math::parse_formula("x");
    ASSERT_TRUE(parsed.ok());
    const engine::ui::math::MathLayout formula =
            engine::ui::math::layout_formula(parsed.root, stix(), engine::ui::math::LayoutOptions{kSize, false});
    const float ascent = kSize * 0.8f;
    const float descent = kSize * 0.2f;
    const float baseline = std::max(ascent, formula.ascent);
    const float line_h = std::max(kSize, baseline + std::max(descent, formula.descent));
    const float line_w = 20.0f + formula.width + 20.0f;

    const engine::ui::Stylesheet sheet = must_parse_css(
            "Label { width: 200px; height: 100px; font-size: 20px; text-align: center; align-items: center; }");
    RecordingPainter painter;
    engine::ui::UiDocument document = must_parse(R"(<Canvas><Label text="A \(x\) B"/></Canvas>)");
    engine::ui::paint_document(document, &sheet, painter,
                               engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 400.f, 200.f},
                                                        .window_width = 400.f,
                                                        .window_height = 200.f});

    ASSERT_GE(painter.texts.size(), 2u);
    EXPECT_EQ(painter.texts[0].text, "A ");
    EXPECT_EQ(painter.texts[1].text, " B");
    const float top = (100.0f - line_h) * 0.5f;
    const float line_left = (200.0f - line_w) * 0.5f;
    EXPECT_NEAR(painter.texts[0].pos.x, line_left, 0.05f);
    EXPECT_NEAR(painter.texts[0].pos.y, top + baseline - ascent, 0.05f);
    EXPECT_NEAR(painter.texts[1].pos.x, line_left + 20.0f + formula.width, 0.05f);
    EXPECT_FALSE(painter.paths.empty());
}

TEST(UiInlineMath, EscapedAndUnclosedDelimitersPaintAsText) {
    RecordingPainter painter;
    const engine::ui::Stylesheet sheet = must_parse_css("Label { font-size: 20px; }");
    engine::ui::UiDocument escaped = must_parse(R"(<Canvas><Label text="a\\(b"/></Canvas>)");
    engine::ui::paint_document(escaped, &sheet, painter,
                               engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 400.f, 200.f},
                                                        .window_width = 400.f,
                                                        .window_height = 200.f});
    ASSERT_EQ(painter.texts.size(), 1u);
    EXPECT_EQ(painter.texts[0].text, R"(a\(b)");
    EXPECT_TRUE(painter.paths.empty());

    painter.texts.clear();
    engine::ui::UiDocument unclosed = must_parse(R"(<Canvas><Label text="value \(x"/></Canvas>)");
    engine::ui::paint_document(unclosed, &sheet, painter,
                               engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 400.f, 200.f},
                                                        .window_width = 400.f,
                                                        .window_height = 200.f});
    ASSERT_EQ(painter.texts.size(), 1u);
    EXPECT_EQ(painter.texts[0].text, R"(value \(x)");
    EXPECT_TRUE(painter.paths.empty());
}

TEST(UiInlineMath, NowrapKeepsTheFormulaOnOneLine) {
    const engine::ui::Stylesheet wrap_sheet = must_parse_css("Label { font-size: 20px; }");
    const engine::ui::Stylesheet nowrap_sheet = must_parse_css("Label { font-size: 20px; white-space: nowrap; }");
    RecordingPainter painter;
    engine::ui::UiDocument wrapped = must_parse(R"(<Canvas><Label text="aa&#10;\(x\)bb"/></Canvas>)");
    engine::ui::UiDocument nowrap = must_parse(R"(<Canvas><Label text="aa&#10;\(x\)bb"/></Canvas>)");
    engine::ui::apply_layout_style(wrapped.root, &wrap_sheet);
    engine::ui::apply_layout_style(nowrap.root, &nowrap_sheet);
    engine::ui::layout(wrapped, {0.f, 0.f, 800.f, 400.f}, &painter);
    engine::ui::layout(nowrap, {0.f, 0.f, 800.f, 400.f}, &painter);
    EXPECT_GT(only_child(nowrap).layout_rect.w, only_child(wrapped).layout_rect.w);
    EXPECT_LT(only_child(nowrap).layout_rect.h, only_child(wrapped).layout_rect.h);
}

TEST(UiInlineMath, LineHeightIsAMinimumStrideForAShortFormula) {
    constexpr float kSize = 20.0f;
    const engine::ui::math::ParseResult parsed = engine::ui::math::parse_formula("x");
    ASSERT_TRUE(parsed.ok());
    const engine::ui::math::MathLayout formula =
            engine::ui::math::layout_formula(parsed.root, stix(), engine::ui::math::LayoutOptions{kSize, false});
    const float ascent = kSize * 0.8f;
    const float descent = kSize * 0.2f;
    const float strut = 40.0f;
    const float baseline = std::max(ascent, formula.ascent);
    const float line_h = std::max(strut, baseline + std::max(descent, formula.descent));

    const engine::ui::Stylesheet sheet = must_parse_css("Label { font-size: 20px; line-height: 2; }");
    RecordingPainter painter;
    engine::ui::UiDocument document = must_parse(R"xml(<Canvas><Label text="\(x\)"/></Canvas>)xml");
    engine::ui::apply_layout_style(document.root, &sheet);
    engine::ui::layout(document, {0.f, 0.f, 400.f, 200.f}, &painter);
    EXPECT_NEAR(only_child(document).layout_rect.h, line_h, 0.05f);
    EXPECT_NEAR(only_child(document).layout_rect.w, formula.width, 0.05f);
}

// First paint measures every run as zero wide (what NanoVG returns under CSS scale(0)). The next
// paint, with real widths, must place the formula after the text instead of reusing that line.
class CollapseThenMeasurePainter final : public RecordingPainter {
public:
    bool collapse = true;

    glm::vec2 measure_text(std::string_view text, engine::AssetId font, float size) override {
        if (collapse) {
            return {0.0f, size};
        }
        return RecordingPainter::measure_text(text, font, size);
    }
};

TEST(UiInlineMath, ZeroWidthMeasureIsNotReused) {
    CollapseThenMeasurePainter painter;
    const engine::ui::Stylesheet sheet = must_parse_css("Label { width: 300px; height: 40px; font-size: 20px; }");
    engine::ui::UiDocument document = must_parse(R"xml(<Canvas><Label text="A \(x\)"/></Canvas>)xml");
    const engine::ui::UiPaintInput input{
            .canvas_rect = {0.f, 0.f, 400.f, 200.f},
            .window_width = 400.f,
            .window_height = 200.f,
    };
    const auto min_glyph_x = [](const std::vector<PathCall>& paths) {
        float x = 1.0e9f;
        for (const PathCall& path : paths) {
            for (const engine::ui::PathSegment& segment : path.segments) {
                x = std::min(x, segment.p.x);
            }
        }
        return x;
    };
    engine::ui::paint_document(document, &sheet, painter, input);
    ASSERT_FALSE(painter.paths.empty());
    EXPECT_LT(min_glyph_x(painter.paths), 5.0f);

    painter.collapse = false;
    painter.paths.clear();
    painter.texts.clear();
    engine::ui::paint_document(document, &sheet, painter, input);
    ASSERT_FALSE(painter.paths.empty());
    const float recovered_x = min_glyph_x(painter.paths);
    // "A " is 2 * 20 * 0.5. A reused zero-width line would still draw the formula at x ≈ 0.
    EXPECT_GT(recovered_x, 15.0f);
}
