#include <gtest/gtest.h>

#include "fixtures/fatal_fakes.h"
#include "ui/picture.h"
#include "ui/svg_parser.h"

#include <engine/resources/asset_guid.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/meta.h>
#include <engine/ui/document.h>
#include <engine/ui/svg.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

class NullPainter final : public engine::ui::IUiPainter {
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
    void stroke_path(std::span<const engine::ui::PathSegment>, float, glm::vec4) override {}
    void set_font(engine::AssetId, float) override {}
    void fill_text(std::string_view, glm::vec2, glm::vec4, engine::ui::UiAlign, engine::ui::UiAlign) override {}
    void image(engine::AssetId, const engine::render::Rect&) override {}
    void image_repeat(engine::AssetId, const engine::render::Rect&) override {}
    void image_nine_slice(engine::AssetId, const engine::render::Rect&, const engine::ui::BoxInsets&) override {}
    glm::vec2 measure_text(std::string_view, engine::AssetId, float) override { return {}; }
};

std::optional<engine::ui::SvgPicture> icon(std::string_view body) {
    const std::string xml = std::string(
                                    "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" "
                                    "stroke-width=\"2\">") +
                            std::string(body) + "</svg>";
    return engine::ui::parse_svg(xml);
}

glm::vec2 cubic_mid(glm::vec2 from, const engine::ui::PathSegment& segment) {
    constexpr float t = 0.5f;
    constexpr float u = 1.0f - t;
    return u * u * u * from + 3.0f * u * u * t * segment.c1 + 3.0f * u * t * t * segment.c2 + t * t * t * segment.p;
}

TEST(UiSvg, StrokeCrossUsesCurrentColorAndTheFileWidth) {
    const auto picture = icon("<path d=\"M5 12 h14\"/><path d=\"M12 5 V19\"/>");
    ASSERT_TRUE(picture.has_value());
    ASSERT_EQ(picture->shapes.size(), 2u);
    EXPECT_FLOAT_EQ(picture->width, 24.0f);
    EXPECT_FLOAT_EQ(picture->height, 24.0f);
    for (const engine::ui::SvgShape& shape : picture->shapes) {
        EXPECT_EQ(shape.paint, engine::ui::SvgShape::Paint::Stroke);
        EXPECT_TRUE(shape.current_color);
        EXPECT_FLOAT_EQ(shape.width, 2.0f);
    }
    const engine::ui::SvgShape& horizontal = picture->shapes[0];
    ASSERT_GE(horizontal.segments.size(), 2u);
    EXPECT_EQ(horizontal.segments[0].kind, engine::ui::PathSegment::Kind::Move);
    EXPECT_EQ(horizontal.segments[1].kind, engine::ui::PathSegment::Kind::Line);
    EXPECT_FLOAT_EQ(horizontal.segments[0].p.x, 5.0f);
    EXPECT_FLOAT_EQ(horizontal.segments[0].p.y, 12.0f);
    EXPECT_FLOAT_EQ(horizontal.segments[1].p.x, 19.0f);
    EXPECT_FLOAT_EQ(horizontal.segments[1].p.y, 12.0f);
}

TEST(UiSvg, FillKeepsAHoleAndALiteralColor) {
    const auto picture = engine::ui::parse_svg(
            "<svg viewBox=\"0 0 10 10\">"
            "<path d=\"M0 0 H10 V10 H0 Z M2 2 H8 V8 H2 Z\" fill=\"#0000ff\"/>"
            "</svg>");
    ASSERT_TRUE(picture.has_value());
    ASSERT_EQ(picture->shapes.size(), 1u);
    const engine::ui::SvgShape& shape = picture->shapes[0];
    EXPECT_EQ(shape.paint, engine::ui::SvgShape::Paint::Fill);
    EXPECT_FALSE(shape.current_color);
    EXPECT_NEAR(shape.color.b, 1.0f, 0.01f);
    int moves = 0;
    int closes = 0;
    for (const engine::ui::PathSegment& segment : shape.segments) {
        moves += segment.kind == engine::ui::PathSegment::Kind::Move ? 1 : 0;
        closes += segment.kind == engine::ui::PathSegment::Kind::Close ? 1 : 0;
    }
    EXPECT_EQ(moves, 2);
    EXPECT_EQ(closes, 2);
}

TEST(UiSvg, GroupTransformScalesThenTranslates) {
    const auto picture = engine::ui::parse_svg(
            "<svg viewBox=\"0 0 20 20\" fill=\"none\" stroke=\"#ffffff\" stroke-width=\"1\">"
            "<g transform=\"translate(5) scale(2)\">"
            "<path d=\"M1 0 L2 0\"/>"
            "</g></svg>");
    ASSERT_TRUE(picture.has_value());
    ASSERT_EQ(picture->shapes.size(), 1u);
    const engine::ui::SvgShape& shape = picture->shapes[0];
    EXPECT_FLOAT_EQ(shape.width, 2.0f);
    ASSERT_GE(shape.segments.size(), 2u);
    EXPECT_FLOAT_EQ(shape.segments[0].p.x, 7.0f);
    EXPECT_FLOAT_EQ(shape.segments[1].p.x, 9.0f);
}

TEST(UiSvg, CircleAndArcBecomeCubicsOnTheCurve) {
    const auto circle = engine::ui::parse_svg(
            "<svg viewBox=\"0 0 20 20\"><circle cx=\"10\" cy=\"10\" r=\"10\" fill=\"#000000\"/></svg>");
    ASSERT_TRUE(circle.has_value());
    ASSERT_EQ(circle->shapes.size(), 1u);
    const auto& segments = circle->shapes[0].segments;
    ASSERT_GE(segments.size(), 2u);
    EXPECT_EQ(segments[0].kind, engine::ui::PathSegment::Kind::Move);
    EXPECT_EQ(segments[1].kind, engine::ui::PathSegment::Kind::Cubic);
    const glm::vec2 mid = cubic_mid(segments[0].p, segments[1]);
    EXPECT_NEAR(mid.x, 17.071f, 0.02f);
    EXPECT_NEAR(mid.y, 17.071f, 0.02f);

    const auto arc = engine::ui::parse_svg(
            "<svg viewBox=\"-10 -10 20 20\" fill=\"none\" stroke=\"#000000\">"
            "<path d=\"M10 0 A10 10 0 0 1 0 10\"/></svg>");
    ASSERT_TRUE(arc.has_value());
    ASSERT_EQ(arc->shapes.size(), 1u);
    ASSERT_GE(arc->shapes[0].segments.size(), 2u);
    const glm::vec2 arc_mid = cubic_mid(arc->shapes[0].segments[0].p, arc->shapes[0].segments[1]);
    EXPECT_NEAR(arc_mid.x, 7.071f, 0.05f);
    EXPECT_NEAR(arc_mid.y, 7.071f, 0.05f);
}

TEST(UiSvg, SmoothCommandsReflectThePreviousControl) {
    const auto picture = engine::ui::parse_svg(
            "<svg viewBox=\"0 0 40 20\" fill=\"none\" stroke=\"#000000\">"
            "<path d=\"M0 0 C0 10 10 10 10 0 S20 -10 20 0\"/>"
            "<path d=\"M0 0 Q10 10 20 0 T40 0\"/>"
            "</svg>");
    ASSERT_TRUE(picture.has_value());
    ASSERT_EQ(picture->shapes.size(), 2u);
    const auto& cubic = picture->shapes[0].segments;
    ASSERT_EQ(cubic.size(), 3u);
    EXPECT_EQ(cubic[2].kind, engine::ui::PathSegment::Kind::Cubic);
    EXPECT_FLOAT_EQ(cubic[2].c1.x, 10.0f);
    EXPECT_FLOAT_EQ(cubic[2].c1.y, -10.0f);
    EXPECT_FLOAT_EQ(cubic[2].p.x, 20.0f);
    const auto& quad = picture->shapes[1].segments;
    ASSERT_EQ(quad.size(), 3u);
    EXPECT_EQ(quad[2].kind, engine::ui::PathSegment::Kind::Quad);
    EXPECT_FLOAT_EQ(quad[2].c1.x, 30.0f);
    EXPECT_FLOAT_EQ(quad[2].c1.y, -10.0f);
    EXPECT_FLOAT_EQ(quad[2].p.x, 40.0f);
}

TEST(UiSvg, LiteralStrokeOverridesCurrentColor) {
    const auto picture = engine::ui::parse_svg(
            "<svg viewBox=\"0 0 10 10\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1\">"
            "<path d=\"M0 0 H10\" stroke=\"#00ff00\"/>"
            "<path d=\"M0 5 H10\"/>"
            "</svg>");
    ASSERT_TRUE(picture.has_value());
    ASSERT_EQ(picture->shapes.size(), 2u);
    EXPECT_FALSE(picture->shapes[0].current_color);
    EXPECT_NEAR(picture->shapes[0].color.g, 1.0f, 0.01f);
    EXPECT_TRUE(picture->shapes[1].current_color);
}

TEST(UiSvg, RejectsWhatThePainterCannotDraw) {
    EXPECT_FALSE(engine::ui::parse_svg("<svg><path d=\"M0 0 H1\"/></svg>").has_value());
    EXPECT_FALSE(engine::ui::parse_svg("<svg viewBox=\"0 0 1 1\"><text>Hi</text></svg>").has_value());
    EXPECT_FALSE(
            engine::ui::parse_svg("<svg viewBox=\"0 0 1 1\"><path d=\"M0 0 H1\" fill-rule=\"evenodd\"/></svg>")
                    .has_value());
    EXPECT_FALSE(
            engine::ui::parse_svg("<svg viewBox=\"0 0 1 1\"><path d=\"M0 0 H1\" stroke-dasharray=\"4 2\"/></svg>")
                    .has_value());
    EXPECT_FALSE(engine::ui::parse_svg("<svg viewBox=\"0 0 1 1\"><path d=\"M0 0 B1 1\"/></svg>").has_value());
    EXPECT_TRUE(
            engine::ui::parse_svg(
                    "<svg viewBox=\"0 0 1 1\" stroke-dasharray=\"none\">"
                    "<path d=\"M0 0 H1\" fill=\"none\" stroke=\"#000\"/></svg>")
                    .has_value());
}

TEST(UiSvg, ElementReadsAnAssetIdAndRejectsAFilename) {
    const auto parsed =
            engine::ui::parse_xml("<Canvas><Svg source=\"0123456789abcdef0123456789abcdef\"/></Canvas>", nullptr);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->root.children.size(), 1u);
    EXPECT_EQ(parsed->root.children[0].kind, engine::ui::ElementKind::Svg);
    ASSERT_TRUE(parsed->root.children[0].source.has_value());
    EXPECT_EQ(parsed->root.children[0].source->hex(), "0123456789abcdef0123456789abcdef");

    fakes::RecordingFatal fatal;
    EXPECT_THROW(
            (void) engine::ui::parse_xml("<Canvas><Svg source=\"icon.svg\"/></Canvas>", &fatal), fakes::FatalReport);
    const auto rejected = engine::ui::parse_xml("<Canvas><Svg source=\"icon.svg\"/></Canvas>", nullptr);
    EXPECT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error(), engine::ui::UiError::ForbiddenContent);
}

TEST(UiSvg, PaintFitsTheViewBoxAndKeepsALiteralColor) {
    constexpr char kXml[] = "<Canvas><Svg source=\"0123456789abcdef0123456789abcdef\"/></Canvas>";
    constexpr char kCss[] = "Svg { width: 40px; height: 20px; color: #ff0000; }";
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(kCss, warnings);
    ASSERT_TRUE(sheet.has_value());
    const auto parsed = engine::ui::parse_xml(kXml, nullptr);
    ASSERT_TRUE(parsed.has_value());
    engine::ui::UiDocument document = std::move(*parsed);
    const auto picture = engine::ui::parse_svg(
            "<svg viewBox=\"0 0 10 10\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\">"
            "<path d=\"M0 5 H10\"/>"
            "<path d=\"M1 1 H4 V4 H1 Z\" fill=\"#0000ff\" stroke=\"none\"/>"
            "</svg>");
    ASSERT_TRUE(picture.has_value());
    document.root.children[0].svg = std::make_shared<const engine::ui::SvgPicture>(*picture);

    NullPainter inner;
    engine::ui::Picture recorded;
    engine::ui::RecordingPainter painter(inner, recorded);
    const engine::ui::UiPaintInput input{
            .canvas_rect = {0.0f, 0.0f, 200.0f, 100.0f}, .window_width = 200.0f, .window_height = 100.0f};
    engine::ui::paint_document(document, &*sheet, painter, input);

    const engine::ui::Element& icon = document.root.children[0];
    EXPECT_FLOAT_EQ(icon.layout_rect.w, 40.0f);
    EXPECT_FLOAT_EQ(icon.layout_rect.h, 20.0f);

    const engine::ui::Picture::StrokePath* stroke = nullptr;
    const engine::ui::Picture::Path* fill = nullptr;
    const auto walk = [&](const auto& self, const engine::ui::Picture& picture) -> void {
        for (const engine::ui::Picture::Command& command : picture.commands) {
            if (const auto* stroke_cmd = std::get_if<engine::ui::Picture::StrokePath>(&command)) {
                stroke = stroke_cmd;
            } else if (const auto* fill_cmd = std::get_if<engine::ui::Picture::Path>(&command)) {
                fill = fill_cmd;
            } else if (const auto* call = std::get_if<engine::ui::Picture::Call>(&command)) {
                if (call->picture) {
                    self(self, *call->picture);
                }
            }
        }
    };
    walk(walk, recorded);
    ASSERT_NE(stroke, nullptr);
    ASSERT_NE(fill, nullptr);
    EXPECT_NEAR(stroke->width, 4.0f, 0.01f);
    EXPECT_NEAR(stroke->color.r, 1.0f, 0.01f);
    EXPECT_NEAR(stroke->color.g, 0.0f, 0.01f);
    ASSERT_GE(stroke->segments.size(), 2u);
    EXPECT_NEAR(stroke->segments[0].p.x, icon.layout_rect.x + 10.0f, 0.01f);
    EXPECT_NEAR(stroke->segments[0].p.y, icon.layout_rect.y + 10.0f, 0.01f);
    EXPECT_NEAR(stroke->segments[1].p.x, icon.layout_rect.x + 30.0f, 0.01f);
    EXPECT_NEAR(fill->color.b, 1.0f, 0.01f);
    EXPECT_NEAR(fill->color.r, 0.0f, 0.01f);
}

struct TempTree {
    std::filesystem::path path;

    TempTree() {
        static int seq = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("wind_svg_" + std::to_string(stamp) + "_" + std::to_string(++seq));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    ~TempTree() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    TempTree(const TempTree&) = delete;
    TempTree& operator=(const TempTree&) = delete;
};

void write_file(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << text;
}

TEST(UiSvg, AssetImporterLoadsThePicture) {
    TempTree tree;
    write_file(
            tree.path / "icon.svg",
            "<svg viewBox=\"0 0 4 4\" fill=\"none\" stroke=\"#fff\" stroke-width=\"1\"><path d=\"M0 0 H4\"/></svg>");
    EXPECT_EQ(engine::write_missing_metas(tree.path), 1);
    const auto meta = engine::parse_asset_meta([&] {
        std::ifstream in(tree.path / "icon.svg.meta");
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }());
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->importer, engine::ImporterKind::Svg);

    fakes::QuietFatal fatal;
    engine::AssetsDb db(fatal);
    db.set_root(tree.path);
    engine::CookedCatalog catalog;
    catalog.add({meta->guid, "icon.svg", engine::ImporterKind::Svg});
    db.set_catalog(std::move(catalog));

    const auto picture = db.get<engine::ui::SvgPicture>(meta->guid);
    ASSERT_NE(picture, nullptr);
    ASSERT_EQ(picture->shapes.size(), 1u);
    EXPECT_EQ(picture->shapes[0].paint, engine::ui::SvgShape::Paint::Stroke);

    write_file(tree.path / "broken.svg", "<svg viewBox=\"0 0 1 1\"><text>Hi</text></svg>");
    engine::CookedCatalog broken_catalog;
    const engine::AssetId broken{"abcdef0123456789abcdef0123456789"};
    broken_catalog.add({broken, "broken.svg", engine::ImporterKind::Svg});
    db.set_catalog(std::move(broken_catalog));
    const auto failed = db.try_get<engine::ui::SvgPicture>(broken);
    EXPECT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error(), engine::AssetError::Corrupt);
}

}
