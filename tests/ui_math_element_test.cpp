#include <gtest/gtest.h>

#include "ui/math/math_element.h"
#include "ui/math/math_font.h"
#include "ui/math/math_layout.h"
#include "ui/math/math_parser.h"
#include "ui/painter.h"
#include "ui/ui_refs.h"

#include <engine/builtin_ids.h>
#include <engine/resources/fatal_error.h>
#include <engine/ui/bindable.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/builder.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

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

#if defined(NANOVG_H) || defined(NANOVG_GL_H) || defined(NANOVG_GL3)
#error "math element tests must not include nvg headers"
#endif

namespace {

using namespace engine::ui;

const math::MathFont& stix() {
    static const math::MathFont font = [] {
        const std::filesystem::path path = std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR} / "fonts" / "math.otf";
        std::ifstream in(path, std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto loaded = math::MathFont::load(bytes);
        EXPECT_TRUE(loaded.has_value());
        return std::move(*loaded);
    }();
    return font;
}

struct PathCall {
    std::vector<PathSegment> segments;
    glm::vec4 color;
};

// A painter that, like the NanoVG one once the engine has registered the builtin math font, exposes a MathFont;
// `has_font` lets a test start without one and add it later.
class MathPainter final : public IUiPainter {
public:
    bool has_font = true;
    std::vector<PathCall> paths;
    std::vector<engine::render::Rect> rects;

    [[nodiscard]] const math::MathFont* math_font() const override { return has_font ? &stix() : nullptr; }

    void save() override {}
    void restore() override {}
    void scissor(const engine::render::Rect&) override {}
    void apply_transform(glm::vec2, float, float) override {}
    void apply_view(glm::vec2, glm::vec2, float) override {}
    void set_opacity(float) override {}
    void fill_rounded_rect(const engine::render::Rect& rect, float, glm::vec4) override { rects.push_back(rect); }
    void fill_rounded_rect_gradient(const engine::render::Rect&, float, const Gradient&) override {}
    void stroke_rounded_rect(const engine::render::Rect&, float, float, glm::vec4) override {}
    void draw_line(glm::vec2, glm::vec2, glm::vec4, float) override {}
    void stroke_arc(glm::vec2, float, float, float, float, glm::vec4) override {}
    void fill_path(std::span<const PathSegment> path, glm::vec4 color) override {
        paths.push_back({std::vector<PathSegment>(path.begin(), path.end()), color});
    }
    void set_font(engine::AssetId, float) override {}
    void fill_text(std::string_view, glm::vec2, glm::vec4, UiAlign, UiAlign) override {}
    void image(engine::AssetId, const engine::render::Rect&) override {}
    void image_repeat(engine::AssetId, const engine::render::Rect&) override {}
    void image_nine_slice(engine::AssetId, const engine::render::Rect&, const BoxInsets&) override {}
    glm::vec2 measure_text(std::string_view text, engine::AssetId, float size) override {
        return {static_cast<float>(text.size()) * size * 0.5f, size};
    }
};

class FormulaVm final : public ViewModel {
public:
    Bindable<std::string> formula;

    FormulaVm() { property(intern("formula"), formula); }
};

class SilentFatal final : public engine::IFatalError {
public:
    int calls = 0;
    void report(std::string_view) override { ++calls; }
};

Stylesheet must_parse_css(std::string_view css) {
    std::vector<std::string> warnings;
    auto sheet = parse_css(css, warnings);
    EXPECT_TRUE(sheet.has_value());
    return *sheet;
}

UiDocument must_parse(std::string_view xml) {
    auto parsed = parse_xml(xml);
    EXPECT_TRUE(parsed.has_value());
    return std::move(*parsed);
}

const Element& only_math(const UiDocument& document) {
    EXPECT_EQ(document.root.children.size(), 1u);
    return document.root.children.at(0);
}

constexpr glm::vec4 kBlue{0.0f, 0.0f, 1.0f, 1.0f};

}

// ---- markup ----------------------------------------------------------------------------------

TEST(UiMathElement, XmlParsesLiteralFormulaAndDisplayFlag) {
    const UiDocument inline_doc = must_parse(R"(<Canvas><Math formula="x^2"/></Canvas>)");
    const Element& inline_math = only_math(inline_doc);
    EXPECT_EQ(inline_math.kind, ElementKind::Math);
    EXPECT_EQ(inline_math.text, "x^2");
    EXPECT_FALSE(inline_math.math_display);
    EXPECT_FALSE(is_bound(inline_math.text_binding));

    const UiDocument display_doc = must_parse(R"(<Canvas><Math formula="\sum_{i=0}^n i" display="true"/></Canvas>)");
    EXPECT_TRUE(only_math(display_doc).math_display);

    const UiDocument off_doc = must_parse(R"(<Canvas><Math formula="x" display="false"/></Canvas>)");
    EXPECT_FALSE(only_math(off_doc).math_display);
}

TEST(UiMathElement, XmlBindsTheFormulaToAViewModelProperty) {
    FormulaVm vm;
    vm.formula = R"(\frac{a}{b})";
    auto parsed = parse_xml(R"(<Canvas><Math formula="{binding formula}"/></Canvas>)", nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const Element& math = only_math(*parsed);
    EXPECT_EQ(math.text_binding, intern("formula"));

    ASSERT_TRUE(apply_bindings(*parsed, vm).has_value());
    EXPECT_EQ(only_math(*parsed).text, R"(\frac{a}{b})");

    vm.formula = "x^2";
    ASSERT_TRUE(apply_bindings(*parsed, vm).has_value());
    EXPECT_EQ(only_math(*parsed).text, "x^2");
}

TEST(UiMathElement, UnregisteredFormulaBindingIsFatalLikeAnyOtherBinding) {
    FormulaVm vm;
    SilentFatal fatal;
    const auto parsed = parse_xml(R"(<Canvas><Math formula="{binding nope}"/></Canvas>)", &fatal, &vm);
    EXPECT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), UiError::MissingBinding);
    EXPECT_GE(fatal.calls, 1);
}

TEST(UiMathElement, BuilderMatchesXml) {
    auto built = make_document(canvas().add(math_formula().formula(R"(\sqrt{x})").math_display(true)));
    ASSERT_TRUE(built.has_value());
    const Element& math = only_math(*built);
    EXPECT_EQ(math.kind, ElementKind::Math);
    EXPECT_EQ(math.text, R"(\sqrt{x})");
    EXPECT_TRUE(math.math_display);

    auto bound = make_document(canvas().add(math_formula().formula_bind(intern("formula"))));
    ASSERT_TRUE(bound.has_value());
    EXPECT_EQ(only_math(*bound).text_binding, intern("formula"));
}

TEST(UiMathElement, CssTypeSelectorMatchesMath) {
    UiDocument document = must_parse(R"(<Canvas><Math formula="x"/></Canvas>)");
    const Stylesheet sheet = must_parse_css("Math { color: #0000ff; }");
    MathPainter painter;
    paint_document(document, &sheet, painter, UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});
    ASSERT_EQ(painter.paths.size(), 1u);
    EXPECT_EQ(painter.paths[0].color, kBlue);
}

TEST(UiMathElement, FontCollectionAddsTheMathFontOnlyForDocumentsWithAFormula) {
    const UiDocument with = must_parse(R"(<Canvas><Label text="a"/><Math formula="x"/></Canvas>)");
    const auto fonts = collect_referenced_fonts(with, nullptr);
    EXPECT_NE(std::find(fonts.begin(), fonts.end(), engine::builtin::font_math), fonts.end());

    const UiDocument without = must_parse(R"(<Canvas><Label text="a"/></Canvas>)");
    const auto none = collect_referenced_fonts(without, nullptr);
    EXPECT_EQ(std::find(none.begin(), none.end(), engine::builtin::font_math), none.end());

    // A formula nested in a Stack is found too.
    const UiDocument nested = must_parse(R"(<Canvas><Stack><Math formula="x"/></Stack></Canvas>)");
    const auto nested_fonts = collect_referenced_fonts(nested, nullptr);
    EXPECT_NE(std::find(nested_fonts.begin(), nested_fonts.end(), engine::builtin::font_math), nested_fonts.end());
}

// ---- layout ----------------------------------------------------------------------------------

TEST(UiMathElement, HugSizeIsTheFormulaLayoutPlusPadding) {
    UiDocument document = must_parse(R"(<Canvas><Math formula="\frac{a}{b}" display="true"/></Canvas>)");
    const Stylesheet sheet = must_parse_css("Math { font-size: 40px; padding: 6px; }");
    MathPainter painter;
    apply_layout_style(document.root, &sheet);
    layout(document, {0.f, 0.f, 800.f, 600.f}, &painter);

    const math::ParseResult parsed = math::parse_formula(R"(\frac{a}{b})");
    const math::MathLayout expected = math::layout_formula(parsed.root, stix(), math::LayoutOptions{40.0f, true});
    const Element& math = only_math(document);
    EXPECT_NEAR(math.layout_rect.w, expected.width + 12.0f, 0.01f);
    EXPECT_NEAR(math.layout_rect.h, expected.height() + 12.0f, 0.01f);
}

TEST(UiMathElement, DisplayStyleChangesTheSize) {
    const Stylesheet sheet = must_parse_css("Math { font-size: 40px; }");
    MathPainter painter;
    UiDocument text_doc = must_parse(R"(<Canvas><Math formula="\sum_{i=0}^{n} i"/></Canvas>)");
    UiDocument display_doc = must_parse(R"(<Canvas><Math formula="\sum_{i=0}^{n} i" display="true"/></Canvas>)");
    for (UiDocument* doc : {&text_doc, &display_doc}) {
        apply_layout_style(doc->root, &sheet);
        layout(*doc, {0.f, 0.f, 800.f, 600.f}, &painter);
    }
    EXPECT_GT(only_math(display_doc).layout_rect.h, only_math(text_doc).layout_rect.h);
}

TEST(UiMathElement, FontSizeScalesTheFormula) {
    MathPainter painter;
    UiDocument small = must_parse(R"(<Canvas><Math formula="x^2+y^2"/></Canvas>)");
    UiDocument large = must_parse(R"(<Canvas><Math formula="x^2+y^2"/></Canvas>)");
    const Stylesheet small_sheet = must_parse_css("Math { font-size: 20px; }");
    const Stylesheet large_sheet = must_parse_css("Math { font-size: 40px; }");
    apply_layout_style(small.root, &small_sheet);
    layout(small, {0.f, 0.f, 800.f, 600.f}, &painter);
    apply_layout_style(large.root, &large_sheet);
    layout(large, {0.f, 0.f, 800.f, 600.f}, &painter);
    EXPECT_NEAR(only_math(large).layout_rect.w, only_math(small).layout_rect.w * 2.0f, 0.05f);
    EXPECT_NEAR(only_math(large).layout_rect.h, only_math(small).layout_rect.h * 2.0f, 0.05f);
}

TEST(UiMathElement, ExplicitSizeWinsOverTheFormulaSize) {
    UiDocument document = must_parse(R"(<Canvas><Math formula="x" class="box"/></Canvas>)");
    const Stylesheet sheet = must_parse_css(".box { width: 300px; height: 80px; }");
    MathPainter painter;
    apply_layout_style(document.root, &sheet);
    layout(document, {0.f, 0.f, 800.f, 600.f}, &painter);
    EXPECT_FLOAT_EQ(only_math(document).layout_rect.w, 300.0f);
    EXPECT_FLOAT_EQ(only_math(document).layout_rect.h, 80.0f);
}

TEST(UiMathElement, WithoutAMathFontLayoutFallsBackToARoughTextSize) {
    UiDocument document = must_parse(R"(<Canvas><Math formula="abcd"/></Canvas>)");
    const Stylesheet sheet = must_parse_css("Math { font-size: 20px; }");
    apply_layout_style(document.root, &sheet);
    layout(document, {0.f, 0.f, 800.f, 600.f});  // no painter at all
    EXPECT_FLOAT_EQ(only_math(document).layout_rect.w, 4.0f * 20.0f * 0.5f);
    EXPECT_FLOAT_EQ(only_math(document).layout_rect.h, 20.0f);

    MathPainter no_font;
    no_font.has_font = false;
    layout(document, {0.f, 0.f, 800.f, 600.f}, &no_font);
    EXPECT_FLOAT_EQ(only_math(document).layout_rect.w, 4.0f * 20.0f * 0.5f);
}

TEST(UiMathElement, FormulaWithErrorsStillGetsASize) {
    UiDocument document = must_parse(R"(<Canvas><Math formula="\notacommand + \frac{1}"/></Canvas>)");
    const Stylesheet sheet = must_parse_css("Math { font-size: 30px; }");
    MathPainter painter;
    apply_layout_style(document.root, &sheet);
    layout(document, {0.f, 0.f, 800.f, 600.f}, &painter);
    EXPECT_GT(only_math(document).layout_rect.w, 0.0f);
    EXPECT_GT(only_math(document).layout_rect.h, 0.0f);
}

TEST(UiMathElement, EmptyFormulaIsAnEmptyBox) {
    UiDocument document = must_parse(R"(<Canvas><Math formula=""/></Canvas>)");
    MathPainter painter;
    apply_layout_style(document.root, nullptr);
    layout(document, {0.f, 0.f, 800.f, 600.f}, &painter);
    EXPECT_FLOAT_EQ(only_math(document).layout_rect.w, 0.0f);
    EXPECT_FLOAT_EQ(only_math(document).layout_rect.h, 0.0f);
    paint_document(document, nullptr, painter, UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});
    EXPECT_TRUE(painter.paths.empty());
}

// ---- the layout cache ------------------------------------------------------------------------

TEST(UiMathElementCache, ReturnsTheSameLayoutUntilSomethingItDependsOnChanges) {
    Element element;
    element.kind = ElementKind::Math;
    element.text = R"(\frac{a}{b})";

    const math::MathLayout* first = &math::element_layout(element, stix(), 24.0f);
    EXPECT_EQ(&math::element_layout(element, stix(), 24.0f), first);

    EXPECT_NE(&math::element_layout(element, stix(), 30.0f), first);  // size changed
    const math::MathLayout* at_30 = &math::element_layout(element, stix(), 30.0f);
    EXPECT_EQ(&math::element_layout(element, stix(), 30.0f), at_30);

    element.math_display = true;
    const math::MathLayout* display = &math::element_layout(element, stix(), 30.0f);
    EXPECT_NE(display->height(), 0.0f);
    EXPECT_EQ(&math::element_layout(element, stix(), 30.0f), display);

    element.text = "x";
    const math::MathLayout& changed = math::element_layout(element, stix(), 30.0f);
    EXPECT_EQ(changed.glyphs.size(), 1u);
}

TEST(UiMathElementCache, ClonedElementsShareTheCacheButNeverCorruptEachOther) {
    Element original;
    original.kind = ElementKind::Math;
    original.text = "a+b";
    const math::MathLayout original_layout = math::element_layout(original, stix(), 24.0f);

    Element clone = original;  // an ItemsControl row cloned from a template copies the cache pointer
    EXPECT_EQ(clone.math_cache, original.math_cache);
    EXPECT_EQ(&math::element_layout(clone, stix(), 24.0f), &math::element_layout(original, stix(), 24.0f));

    clone.text = R"(\frac{a}{b})";
    const math::MathLayout& clone_layout = math::element_layout(clone, stix(), 24.0f);
    EXPECT_NE(clone_layout.rules.size(), original_layout.rules.size());

    // The original still gets its own, unchanged layout.
    const math::MathLayout& again = math::element_layout(original, stix(), 24.0f);
    EXPECT_EQ(again.glyphs.size(), original_layout.glyphs.size());
    EXPECT_EQ(again.rules.size(), original_layout.rules.size());
    EXPECT_FLOAT_EQ(again.width, original_layout.width);
}

// ---- dirty gate ------------------------------------------------------------------------------

TEST(UiMathElementDirtyGate, RelaysOutOnceTheMathFontArrives) {
    UiDocument document = must_parse(R"(<Canvas><Math formula="\frac{a}{b}" display="true"/></Canvas>)");
    const Stylesheet sheet = must_parse_css("Math { font-size: 40px; }");
    MathPainter painter;
    painter.has_font = false;
    const UiPaintInput input{.canvas_rect = {0.f, 0.f, 800.f, 600.f}};

    paint_document(document, &sheet, painter, input);
    const float fallback_w = only_math(document).layout_rect.w;
    EXPECT_FLOAT_EQ(fallback_w, 11.0f * 40.0f * 0.5f);  // 11 characters of source at the rough text width
    EXPECT_TRUE(painter.paths.empty());                 // nothing to draw with yet

    // The engine registers the font between frames: same painter, same document, nothing else changed.
    painter.has_font = true;
    paint_document(document, &sheet, painter, input);
    EXPECT_NE(only_math(document).layout_rect.w, fallback_w);
    EXPECT_EQ(painter.paths.size(), 1u);
}

TEST(UiMathElementDirtyGate, ChangingTheBoundFormulaRelaysOut) {
    FormulaVm vm;
    vm.formula = "x";
    auto parsed = parse_xml(R"(<Canvas><Math formula="{binding formula}"/></Canvas>)", nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    UiDocument& document = *parsed;
    const Stylesheet sheet = must_parse_css("Math { font-size: 30px; }");
    MathPainter painter;
    const UiPaintInput input{.canvas_rect = {0.f, 0.f, 800.f, 600.f}};

    ASSERT_TRUE(apply_bindings(document, vm).has_value());
    paint_document(document, &sheet, painter, input);
    const float narrow = only_math(document).layout_rect.w;

    vm.formula = "x^2 + y^2 + z^2 = r^2";
    ASSERT_TRUE(apply_bindings(document, vm).has_value());
    paint_document(document, &sheet, painter, input);
    EXPECT_GT(only_math(document).layout_rect.w, narrow * 3.0f);
}

// ---- painting --------------------------------------------------------------------------------

TEST(UiMathElementPaint, DrawsOnePathInTheCssColorInsideTheContentBox) {
    UiDocument document = must_parse(R"(<Canvas><Math formula="x^2" class="f"/></Canvas>)");
    const Stylesheet sheet = must_parse_css(".f { font-size: 50px; color: #0000ff; padding: 10px; position: absolute; "
                                            "left: 100px; top: 40px; }");
    MathPainter painter;
    paint_document(document, &sheet, painter, UiPaintInput{.canvas_rect = {0.f, 0.f, 800.f, 600.f}});

    ASSERT_EQ(painter.paths.size(), 1u);
    EXPECT_EQ(painter.paths[0].color, kBlue);
    const Element& math = only_math(document);
    const float slack = 0.06f * 50.0f;
    const float content_x = math.layout_rect.x + 10.0f;
    const float content_y = math.layout_rect.y + 10.0f;
    for (const PathSegment& s : painter.paths[0].segments) {
        EXPECT_GE(s.p.x, content_x - slack);
        EXPECT_LE(s.p.x, content_x + (math.layout_rect.w - 20.0f) + slack);
        EXPECT_GE(s.p.y, content_y - slack);
        EXPECT_LE(s.p.y, content_y + (math.layout_rect.h - 20.0f) + slack);
    }
}

TEST(UiMathElementPaint, UiScaleScalesTheDrawnShape) {
    const Stylesheet sheet = must_parse_css("Math { font-size: 30px; }");
    MathPainter one;
    MathPainter two;
    UiDocument a = must_parse(R"(<Canvas><Math formula="\frac{a}{b}"/></Canvas>)");
    UiDocument b = must_parse(R"(<Canvas><Math formula="\frac{a}{b}"/></Canvas>)");
    paint_document(a, &sheet, one, UiPaintInput{.canvas_rect = {0.f, 0.f, 800.f, 600.f}, .ui_scale = 1.0f});
    paint_document(b, &sheet, two, UiPaintInput{.canvas_rect = {0.f, 0.f, 800.f, 600.f}, .ui_scale = 2.0f});
    ASSERT_EQ(one.paths.size(), 1u);
    ASSERT_EQ(two.paths.size(), 1u);
    ASSERT_EQ(one.paths[0].segments.size(), two.paths[0].segments.size());
    for (std::size_t i = 0; i < one.paths[0].segments.size(); ++i) {
        EXPECT_NEAR(two.paths[0].segments[i].p.x, one.paths[0].segments[i].p.x * 2.0f, 0.01f);
        EXPECT_NEAR(two.paths[0].segments[i].p.y, one.paths[0].segments[i].p.y * 2.0f, 0.01f);
    }
    // The fraction bar is a rect, scaled likewise.
    ASSERT_EQ(one.rects.size(), 1u);
    ASSERT_EQ(two.rects.size(), 1u);
    EXPECT_NEAR(two.rects[0].w, one.rects[0].w * 2.0f, 0.01f);
}

TEST(UiMathElementPaint, TextAlignCentersTheFormulaInAWideBox) {
    const Stylesheet start_sheet = must_parse_css("Math { font-size: 30px; width: 400px; }");
    const Stylesheet center_sheet = must_parse_css("Math { font-size: 30px; width: 400px; text-align: center; }");
    const auto extent_x = [](const std::vector<PathSegment>& path) {
        const auto [low, high] = std::minmax_element(
                path.begin(), path.end(), [](const PathSegment& a, const PathSegment& b) { return a.p.x < b.p.x; });
        return std::pair<float, float>{low->p.x, high->p.x};
    };
    MathPainter at_start;
    MathPainter centered;
    UiDocument a = must_parse(R"(<Canvas><Math formula="x+y"/></Canvas>)");
    UiDocument b = must_parse(R"(<Canvas><Math formula="x+y"/></Canvas>)");
    paint_document(a, &start_sheet, at_start, UiPaintInput{.canvas_rect = {0.f, 0.f, 800.f, 600.f}});
    paint_document(b, &center_sheet, centered, UiPaintInput{.canvas_rect = {0.f, 0.f, 800.f, 600.f}});
    ASSERT_EQ(at_start.paths.size(), 1u);
    ASSERT_EQ(centered.paths.size(), 1u);
    const auto [start_lo, start_hi] = extent_x(at_start.paths[0].segments);
    const auto [centre_lo, centre_hi] = extent_x(centered.paths[0].segments);
    EXPECT_LT(start_lo, 20.0f);
    EXPECT_NEAR((centre_lo + centre_hi) * 0.5f, 200.0f, 12.0f);
    EXPECT_GT(centre_lo, start_lo + 100.0f);
    (void)start_hi;
}

TEST(UiMathElementPaint, HiddenOrAbsentFontDrawsNothing) {
    const Stylesheet hidden = must_parse_css("Math { font-size: 30px; visibility: hidden; }");
    UiDocument document = must_parse(R"(<Canvas><Math formula="x"/></Canvas>)");
    MathPainter painter;
    paint_document(document, &hidden, painter, UiPaintInput{.canvas_rect = {0.f, 0.f, 800.f, 600.f}});
    EXPECT_TRUE(painter.paths.empty());

    MathPainter no_font;
    no_font.has_font = false;
    UiDocument other = must_parse(R"(<Canvas><Math formula="x"/></Canvas>)");
    paint_document(other, nullptr, no_font, UiPaintInput{.canvas_rect = {0.f, 0.f, 800.f, 600.f}});
    EXPECT_TRUE(no_font.paths.empty());
}
