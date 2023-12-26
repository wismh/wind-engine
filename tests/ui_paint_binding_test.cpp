#include <gtest/gtest.h>

#include "ui/painter.h"

#include <engine/resources/fatal_error.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/builder.h>
#include <engine/ui/document.h>
#include <engine/ui/draw_list.h>
#include <engine/ui/paint.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <string>
#include <string_view>
#include <vector>

#if defined(NANOVG_H) || defined(NANOVG_GL_H) || defined(NANOVG_GL3)
#error "ui paint binding tests must not include nvg headers"
#endif

namespace {

class RecordingFatalError final : public engine::IFatalError {
public:
    int call_count = 0;
    std::string last_message;

    void report(std::string_view message) override {
        ++call_count;
        last_message = std::string(message);
    }
};

struct LineCall {
    glm::vec2 from{};
    glm::vec2 to{};
    glm::vec4 color{};
    float width = 0.0f;
};

struct ArcCall {
    glm::vec2 center{};
    float radius = 0.0f;
    float start_angle = 0.0f;
    float end_angle = 0.0f;
    float width = 0.0f;
    glm::vec4 color{};
};

class FakePainter final : public engine::ui::IUiPainter {
public:
    std::vector<LineCall> lines;
    std::vector<ArcCall> arcs;

    void save() override {}
    void restore() override {}
    void scissor(const engine::render::Rect&) override {}
    void apply_transform(glm::vec2, float, float) override {}
    void apply_view(glm::vec2, glm::vec2, float) override {}
    void set_opacity(float) override {}
    void fill_rounded_rect(const engine::render::Rect&, float, glm::vec4) override {}
    void fill_rounded_rect_gradient(const engine::render::Rect&, float, const engine::ui::Gradient&) override {}
    void stroke_rounded_rect(const engine::render::Rect&, float, float, glm::vec4) override {}
    void draw_line(glm::vec2 from, glm::vec2 to, glm::vec4 color, float width) override {
        lines.push_back(LineCall{from, to, color, width});
    }
    void stroke_arc(glm::vec2 center, float radius, float start_angle, float end_angle, float width,
            glm::vec4 color) override {
        arcs.push_back(ArcCall{center, radius, start_angle, end_angle, width, color});
    }
    void set_font(engine::AssetId, float) override {}
    void fill_text(std::string_view, glm::vec2, glm::vec4, engine::ui::UiAlign, engine::ui::UiAlign) override {}
    void image(engine::AssetId, const engine::render::Rect&) override {}
    void image_repeat(engine::AssetId, const engine::render::Rect&) override {}
    void image_nine_slice(engine::AssetId, const engine::render::Rect&, const engine::ui::BoxInsets&) override {}
    glm::vec2 measure_text(std::string_view, engine::AssetId, float size) override { return {size, size}; }
};

class PlotVm final : public engine::ui::ViewModel {
public:
    engine::ui::RelayPaint plot;

    PlotVm() {
        plot = [](engine::ui::IDrawList& list, const engine::render::Rect& content) {
            const glm::vec4 color{1.0f, 0.0f, 0.0f, 1.0f};
            list.line({0.0f, 0.0f}, {content.w, content.h}, color, 2.0f);
        };
        paint(engine::ui::intern("plot"), plot);
    }
};

class RingVm final : public engine::ui::ViewModel {
public:
    engine::ui::RelayPaint plot;

    RingVm() {
        plot = [](engine::ui::IDrawList& list, const engine::render::Rect& content) {
            const glm::vec4 color{0.0f, 1.0f, 0.0f, 1.0f};
            list.arc({content.w * 0.5f, content.h * 0.5f}, 10.0f, 0.0f, 1.5f, color, 3.0f);
        };
        paint(engine::ui::intern("plot"), plot);
    }
};

engine::ui::Stylesheet must_parse_css(std::string_view css) {
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(css, warnings);
    EXPECT_TRUE(sheet.has_value());
    return *sheet;
}

}

TEST(UiPaintBinding, ParseComponentPaintBinding) {
    const auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Component class="chart" paint="{binding plot}"/>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->root.children.size(), 1u);
    EXPECT_EQ(parsed->root.children[0].kind, engine::ui::ElementKind::Component);
    EXPECT_EQ(parsed->root.children[0].classes, std::vector<std::string>{"chart"});
    EXPECT_EQ(parsed->root.children[0].paint_binding, engine::ui::intern("plot"));
}

TEST(UiPaintBinding, UnknownTagIsStillFatal) {
    RecordingFatalError fatal;
    const auto parsed = engine::ui::parse_xml("<Canvas><Plot/></Canvas>", &fatal);
    EXPECT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), engine::ui::UiError::UnknownElement);
    EXPECT_GE(fatal.call_count, 1);
}

TEST(UiPaintBinding, BuilderComponentMatchesXml) {
    auto document = engine::ui::make_document(
            engine::ui::canvas().add(engine::ui::component().with_class("chart").paint_bind(engine::ui::intern("plot"))));
    ASSERT_TRUE(document.has_value());
    ASSERT_EQ(document->root.children.size(), 1u);
    EXPECT_EQ(document->root.children[0].kind, engine::ui::ElementKind::Component);
    EXPECT_EQ(document->root.children[0].paint_binding, engine::ui::intern("plot"));
}

TEST(UiPaintBinding, PaintDrawsLineInLocalContentCoordinates) {
    PlotVm vm;
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas><Component class="chart" paint="{binding plot}"/></Canvas>)", nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm).has_value());

    const engine::ui::Stylesheet sheet = must_parse_css(".chart { width: 100; height: 50; padding: 10; }");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}, .ui_scale = 2.0f});

    ASSERT_EQ(painter.lines.size(), 1u);
    EXPECT_FLOAT_EQ(painter.lines[0].from.x, 20.0f);
    EXPECT_FLOAT_EQ(painter.lines[0].from.y, 20.0f);
    EXPECT_FLOAT_EQ(painter.lines[0].to.x, 180.0f);
    EXPECT_FLOAT_EQ(painter.lines[0].to.y, 80.0f);
    EXPECT_FLOAT_EQ(painter.lines[0].width, 4.0f);
    EXPECT_EQ(painter.lines[0].color, (glm::vec4{1.0f, 0.0f, 0.0f, 1.0f}));
}

TEST(UiPaintBinding, PaintDrawsArcInLocalContentCoordinatesWithScaledRadiusAndWidth) {
    RingVm vm;
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas><Component class="chart" paint="{binding plot}"/></Canvas>)", nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm).has_value());

    const engine::ui::Stylesheet sheet = must_parse_css(".chart { width: 100; height: 50; padding: 10; }");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}, .ui_scale = 2.0f});

    // content box is 80x30 local px at screen origin (10,10)*2 = (20,20); local center (40,15) maps
    // to screen (20,20) + (40,15)*2 = (100,50). Radius/width scale by ui_scale, angles do not.
    ASSERT_EQ(painter.arcs.size(), 1u);
    EXPECT_FLOAT_EQ(painter.arcs[0].center.x, 100.0f);
    EXPECT_FLOAT_EQ(painter.arcs[0].center.y, 50.0f);
    EXPECT_FLOAT_EQ(painter.arcs[0].radius, 20.0f);
    EXPECT_FLOAT_EQ(painter.arcs[0].width, 6.0f);
    EXPECT_FLOAT_EQ(painter.arcs[0].start_angle, 0.0f);
    EXPECT_FLOAT_EQ(painter.arcs[0].end_angle, 1.5f);
    EXPECT_EQ(painter.arcs[0].color, (glm::vec4{0.0f, 1.0f, 0.0f, 1.0f}));
}

TEST(UiPaintBinding, UnregisteredPaintIsMissingBinding) {
    engine::ui::ViewModel vm;
    RecordingFatalError fatal;
    const auto parsed = engine::ui::parse_xml(
            R"(<Canvas><Component paint="{binding plot}"/></Canvas>)", &fatal, &vm);
    EXPECT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), engine::ui::UiError::MissingBinding);
    EXPECT_GE(fatal.call_count, 1);
}

TEST(UiPaintBinding, ApplyBindingsResolvesPaintPointer) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Component paint="{binding plot}"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->root.children[0].paint, nullptr);

    PlotVm vm;
    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm).has_value());
    EXPECT_EQ(parsed->root.children[0].paint, &vm.plot);

    engine::ui::ViewModel empty;
    RecordingFatalError fatal;
    EXPECT_EQ(engine::ui::apply_bindings(*parsed, empty, &fatal).error(), engine::ui::UiError::MissingBinding);
}
