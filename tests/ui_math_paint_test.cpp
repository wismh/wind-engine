#include <gtest/gtest.h>

#include "ui/math/math_font.h"
#include "ui/math/math_layout.h"
#include "ui/math/math_paint.h"
#include "ui/math/math_parser.h"
#include "ui/painter.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#ifndef ENGINE_BUILTIN_ASSETS_DIR
#error "ENGINE_BUILTIN_ASSETS_DIR must be set to the builtin_assets path"
#endif

#if defined(NANOVG_H) || defined(NANOVG_GL_H) || defined(NANOVG_GL3)
#error "math paint tests must not include nvg headers"
#endif

namespace {

using namespace engine::ui::math;
using engine::ui::PathSegment;

struct RectCall {
    engine::render::Rect rect;
    float radius;
    glm::vec4 color;
};

struct PathCall {
    std::vector<PathSegment> segments;
    glm::vec4 color;
};

class RecordingPainter final : public engine::ui::IUiPainter {
public:
    std::vector<PathCall> paths;
    std::vector<RectCall> rects;
    int other_calls = 0;

    void save() override { ++other_calls; }
    void restore() override { ++other_calls; }
    void scissor(const engine::render::Rect&) override { ++other_calls; }
    void apply_transform(glm::vec2, float, float) override { ++other_calls; }
    void apply_view(glm::vec2, glm::vec2, float) override { ++other_calls; }
    void set_opacity(float) override { ++other_calls; }
    void fill_rounded_rect(const engine::render::Rect& rect, float radius, glm::vec4 color) override {
        rects.push_back({rect, radius, color});
    }
    void fill_rounded_rect_gradient(const engine::render::Rect&, float, const engine::ui::Gradient&) override {
        ++other_calls;
    }
    void stroke_rounded_rect(const engine::render::Rect&, float, float, glm::vec4) override { ++other_calls; }
    void draw_line(glm::vec2, glm::vec2, glm::vec4, float) override { ++other_calls; }
    void stroke_arc(glm::vec2, float, float, float, float, glm::vec4) override { ++other_calls; }
    void fill_path(std::span<const PathSegment> path, glm::vec4 color) override {
        paths.push_back({std::vector<PathSegment>(path.begin(), path.end()), color});
    }
    void set_font(engine::AssetId, float) override { ++other_calls; }
    void fill_text(std::string_view, glm::vec2, glm::vec4, engine::ui::UiAlign, engine::ui::UiAlign) override {
        ++other_calls;
    }
    void image(engine::AssetId, const engine::render::Rect&) override { ++other_calls; }
    void image_repeat(engine::AssetId, const engine::render::Rect&) override { ++other_calls; }
    void image_nine_slice(engine::AssetId, const engine::render::Rect&, const engine::ui::BoxInsets&) override {
        ++other_calls;
    }
    glm::vec2 measure_text(std::string_view, engine::AssetId, float) override { return {}; }
};

const MathFont& stix() {
    static const MathFont font = [] {
        const std::filesystem::path path = std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR} / "fonts" / "math.otf";
        std::ifstream in(path, std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto loaded = MathFont::load(bytes);
        EXPECT_TRUE(loaded.has_value());
        return std::move(*loaded);
    }();
    return font;
}

MathLayout lay(std::string_view source, bool display = false, float size = 100.0f) {
    const ParseResult parsed = parse_formula(source);
    EXPECT_TRUE(parsed.ok()) << source;
    return layout_formula(parsed.root, stix(), LayoutOptions{size, display});
}

std::size_t count_moves(std::span<const PathSegment> path) {
    return static_cast<std::size_t>(std::count_if(
            path.begin(), path.end(), [](const PathSegment& s) { return s.kind == PathSegment::Kind::Move; }));
}

std::size_t font_moves(GlyphId glyph) {
    const std::vector<OutlineCommand> outline = stix().outline(glyph);
    return static_cast<std::size_t>(std::count_if(outline.begin(), outline.end(),
            [](const OutlineCommand& c) { return c.kind == OutlineCommand::Kind::Move; }));
}

constexpr glm::vec4 kRed{1.0f, 0.0f, 0.0f, 1.0f};

}

TEST(MathPaint, EmptyLayoutDrawsNothing) {
    RecordingPainter painter;
    paint_layout(painter, stix(), lay(""), {5.0f, 5.0f}, 1.0f, kRed);
    EXPECT_TRUE(painter.paths.empty());
    EXPECT_TRUE(painter.rects.empty());
}

TEST(MathPaint, OneGlyphIsOnePathWithItsOutlineMappedToScreen) {
    const MathLayout layout = lay("x");
    ASSERT_EQ(layout.glyphs.size(), 1u);
    RecordingPainter painter;
    const glm::vec2 origin{10.0f, 20.0f};
    constexpr float kScale = 2.0f;
    paint_layout(painter, stix(), layout, origin, kScale, kRed);

    ASSERT_EQ(painter.paths.size(), 1u);
    EXPECT_EQ(painter.paths[0].color, kRed);
    const std::vector<OutlineCommand> outline = stix().outline(layout.glyphs[0].glyph);
    const std::vector<PathSegment>& path = painter.paths[0].segments;
    ASSERT_EQ(path.size(), outline.size());

    const PlacedGlyph& g = layout.glyphs[0];
    for (std::size_t i = 0; i < path.size(); ++i) {
        // y flips (font y is up, screen y is down); x and y scale by the glyph scale and the canvas scale.
        const float want_x = origin.x + (g.origin.x + outline[i].p.x * g.scale) * kScale;
        const float want_y = origin.y + (g.origin.y - outline[i].p.y * g.scale) * kScale;
        EXPECT_NEAR(path[i].p.x, want_x, 1e-3f) << i;
        EXPECT_NEAR(path[i].p.y, want_y, 1e-3f) << i;
    }
    EXPECT_TRUE(painter.rects.empty());
    EXPECT_EQ(painter.other_calls, 0);
}

TEST(MathPaint, GlyphInkLandsInsideTheScaledLayoutBox) {
    const MathLayout layout = lay("x^2");
    RecordingPainter painter;
    const glm::vec2 origin{100.0f, 50.0f};
    constexpr float kScale = 1.5f;
    paint_layout(painter, stix(), layout, origin, kScale, kRed);
    ASSERT_EQ(painter.paths.size(), 1u);
    const float slack = 0.06f * 100.0f * kScale;
    for (const PathSegment& s : painter.paths[0].segments) {
        EXPECT_GE(s.p.x, origin.x - slack);
        EXPECT_LE(s.p.x, origin.x + layout.width * kScale + slack);
        EXPECT_GE(s.p.y, origin.y - slack);
        EXPECT_LE(s.p.y, origin.y + layout.height() * kScale + slack);
    }
}

TEST(MathPaint, AllGlyphsOfAFormulaShareOnePath) {
    const MathLayout layout = lay("a+b=c");
    ASSERT_EQ(layout.glyphs.size(), 5u);
    RecordingPainter painter;
    paint_layout(painter, stix(), layout, {0.0f, 0.0f}, 1.0f, kRed);

    ASSERT_EQ(painter.paths.size(), 1u);
    std::size_t contours = 0;
    for (const PlacedGlyph& g : layout.glyphs) {
        contours += font_moves(g.glyph);
    }
    EXPECT_EQ(count_moves(painter.paths[0].segments), contours);
}

TEST(MathPaint, RulesGoThroughSquareFilledRects) {
    const MathLayout layout = lay("\\frac{a}{b}");
    ASSERT_EQ(layout.rules.size(), 1u);
    RecordingPainter painter;
    const glm::vec2 origin{7.0f, 9.0f};
    constexpr float kScale = 3.0f;
    paint_layout(painter, stix(), layout, origin, kScale, kRed);

    ASSERT_EQ(painter.rects.size(), 1u);
    const RectCall& rect = painter.rects[0];
    EXPECT_FLOAT_EQ(rect.rect.x, origin.x + layout.rules[0].position.x * kScale);
    EXPECT_FLOAT_EQ(rect.rect.y, origin.y + layout.rules[0].position.y * kScale);
    EXPECT_FLOAT_EQ(rect.rect.w, layout.rules[0].size.x * kScale);
    EXPECT_FLOAT_EQ(rect.rect.h, layout.rules[0].size.y * kScale);
    EXPECT_FLOAT_EQ(rect.radius, 0.0f);
    EXPECT_EQ(rect.color, kRed);
    EXPECT_EQ(painter.paths.size(), 1u);  // the numerator and denominator glyphs
}

TEST(MathPaint, PreservesCurveKinds) {
    // The CFF outline of an italic f is cubic; the painter must receive Cubic segments with both controls.
    const MathLayout layout = lay("f");
    RecordingPainter painter;
    paint_layout(painter, stix(), layout, {0.0f, 0.0f}, 1.0f, kRed);
    ASSERT_EQ(painter.paths.size(), 1u);
    const auto& path = painter.paths[0].segments;
    EXPECT_TRUE(std::any_of(path.begin(), path.end(),
            [](const PathSegment& s) { return s.kind == PathSegment::Kind::Cubic; }));
    EXPECT_EQ(path.front().kind, PathSegment::Kind::Move);
}

TEST(MathPaint, RepaintingGivesTheSameGeometryAndMovesWithTheOrigin) {
    const MathLayout layout = lay("\\sqrt{x+1}");
    RecordingPainter painter;
    paint_layout(painter, stix(), layout, {0.0f, 0.0f}, 1.0f, kRed);
    paint_layout(painter, stix(), layout, {0.0f, 0.0f}, 1.0f, kRed);
    paint_layout(painter, stix(), layout, {30.0f, 40.0f}, 1.0f, kRed);
    ASSERT_EQ(painter.paths.size(), 3u);
    ASSERT_EQ(painter.paths[0].segments.size(), painter.paths[1].segments.size());
    ASSERT_EQ(painter.paths[0].segments.size(), painter.paths[2].segments.size());
    for (std::size_t i = 0; i < painter.paths[0].segments.size(); ++i) {
        EXPECT_EQ(painter.paths[0].segments[i].p, painter.paths[1].segments[i].p);
        EXPECT_NEAR(painter.paths[2].segments[i].p.x, painter.paths[0].segments[i].p.x + 30.0f, 1e-3f);
        EXPECT_NEAR(painter.paths[2].segments[i].p.y, painter.paths[0].segments[i].p.y + 40.0f, 1e-3f);
    }
}

TEST(MathPaint, DisplayStyleSumPaintsATallerShape) {
    RecordingPainter painter;
    paint_layout(painter, stix(), lay(R"(\sum)", true), {0.0f, 0.0f}, 1.0f, kRed);
    paint_layout(painter, stix(), lay(R"(\sum)", false), {0.0f, 0.0f}, 1.0f, kRed);
    ASSERT_EQ(painter.paths.size(), 2u);
    const auto extent = [](const std::vector<PathSegment>& path) {
        const auto [low, high] = std::minmax_element(
                path.begin(), path.end(), [](const PathSegment& a, const PathSegment& b) { return a.p.y < b.p.y; });
        return high->p.y - low->p.y;
    };
    EXPECT_GT(extent(painter.paths[0].segments), extent(painter.paths[1].segments) * 1.2f);
}
