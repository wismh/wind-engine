#include <gtest/gtest.h>

#include "ui/painter.h"

#include <engine/builtin_ids.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/fatal_error.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(NANOVG_H) || defined(NANOVG_GL_H) || defined(NANOVG_GL3)
#error "ui painter tests must not include nvg headers"
#endif

namespace {

struct PaintCall {
    std::string op;
    engine::render::Rect rect{};
    glm::vec4 color{};
    float radius = 0.0f;
    float width = 0.0f;
    float opacity = 1.0f;
    float font_size = 0.0f;
    engine::AssetId font{};
    engine::AssetId texture{};
    std::string text;
    glm::vec2 position{};
    engine::ui::UiAlign horizontal = engine::ui::UiAlign::Start;
    engine::ui::UiAlign vertical = engine::ui::UiAlign::Start;
    glm::vec2 transform_center{};
    float rotation_radians = 0.0f;
    float transform_scale = 1.0f;
};

class FakePainter final : public engine::ui::IUiPainter {
public:
    std::vector<PaintCall> calls;

    void save() override { calls.push_back(PaintCall{.op = "save"}); }

    void restore() override { calls.push_back(PaintCall{.op = "restore"}); }

    void scissor(const engine::render::Rect& rect) override {
        calls.push_back(PaintCall{.op = "scissor", .rect = rect});
    }

    void apply_transform(glm::vec2 center, float rotation_radians, float scale) override {
        calls.push_back(PaintCall{
                .op = "transform", .transform_center = center, .rotation_radians = rotation_radians,
                .transform_scale = scale});
    }

    void set_opacity(float opacity) override {
        calls.push_back(PaintCall{.op = "opacity", .opacity = opacity});
    }

    void fill_rounded_rect(const engine::render::Rect& rect, float radius, glm::vec4 color) override {
        calls.push_back(PaintCall{.op = "fill_rect", .rect = rect, .color = color, .radius = radius});
    }

    void stroke_rounded_rect(const engine::render::Rect& rect, float radius, float width, glm::vec4 color) override {
        calls.push_back(PaintCall{.op = "stroke_rect", .rect = rect, .color = color, .radius = radius, .width = width});
    }

    void set_font(engine::AssetId font, float size) override {
        calls.push_back(PaintCall{.op = "font", .font_size = size, .font = font});
    }

    void fill_text(std::string_view text, glm::vec2 position, glm::vec4 color, engine::ui::UiAlign horizontal,
            engine::ui::UiAlign vertical) override {
        calls.push_back(PaintCall{.op = "text", .color = color, .text = std::string(text), .position = position,
                .horizontal = horizontal, .vertical = vertical});
    }

    void image(engine::AssetId texture, const engine::render::Rect& rect) override {
        calls.push_back(PaintCall{.op = "image", .rect = rect, .texture = texture});
    }

    glm::vec2 measure_text(std::string_view text, engine::AssetId, float size) override {
        return {static_cast<float>(text.size()) * size * 0.5f, size};
    }

    [[nodiscard]] int count(std::string_view op) const {
        int n = 0;
        for (const PaintCall& call : calls) {
            if (call.op == op) {
                ++n;
            }
        }
        return n;
    }

    [[nodiscard]] const PaintCall* find(std::string_view op) const {
        for (const PaintCall& call : calls) {
            if (call.op == op) {
                return &call;
            }
        }
        return nullptr;
    }
};

class TitleVm final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<std::string> title{"Score"};

    TitleVm() { property(engine::ui::intern("title"), title); }
};

class CellVm final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<std::string> mark;
    engine::ui::RelayCommand click;

    CellVm() {
        property(engine::ui::intern("mark"), mark);
        command(engine::ui::intern("click"), click);
    }
};

class BoardVm final : public engine::ui::ViewModel {
public:
    engine::ui::BindableList<std::shared_ptr<CellVm>> cells;

    BoardVm() { property(engine::ui::intern("cells"), cells); }
};

class ToggleVm final : public engine::ui::ViewModel {
public:
    engine::ui::RelayCommand go;
    bool enabled = true;

    ToggleVm() {
        command(engine::ui::intern("go"), go);
        go.set_can_execute(true);
        go = [] {};
    }
};

class IconVm final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<engine::AssetId> icon;

    explicit IconVm(engine::AssetId id) : icon(id) { property(engine::ui::intern("icon"), icon); }
};

class StringIconVm final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<std::string> icon;

    explicit StringIconVm(std::string value) : icon(std::move(value)) {
        property(engine::ui::intern("icon"), icon);
    }
};

class RecordingFatalError final : public engine::IFatalError {
public:
    int call_count = 0;
    std::string last_message;

    void report(std::string_view message) override {
        ++call_count;
        last_message = std::string(message);
    }
};

constexpr engine::AssetId kIconGuid{"c1a1c2d3e4f5678901234567890abc09"};
constexpr engine::AssetId kHoverImage{"c1a1c2d3e4f5678901234567890abc0a"};
constexpr float kDefaultImageSize = 32.0f;
constexpr float kFakeFontSize = 16.0f;

[[nodiscard]] const PaintCall* find_image(const FakePainter& painter, engine::AssetId texture) {
    for (const PaintCall& call : painter.calls) {
        if (call.op == "image" && call.texture == texture) {
            return &call;
        }
    }
    return nullptr;
}

[[nodiscard]] float fake_text_width(std::string_view text, float size = kFakeFontSize) {
    return static_cast<float>(text.size()) * size * 0.5f;
}

engine::ui::Stylesheet must_parse_css(std::string_view css) {
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(css, warnings);
    EXPECT_TRUE(sheet.has_value());
    return *sheet;
}

}

TEST(UiPainter, LabelTextFromXmlAndCssColor) {
    TitleVm vm;
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Label class="title" text="{binding title}"/></Canvas>)", nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm).has_value());

    const engine::ui::Stylesheet sheet = must_parse_css(".title { color: #ff0000; font-size: 24; }");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    const PaintCall* text = painter.find("text");
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->text, "Score");
    EXPECT_FLOAT_EQ(text->color.r, 1.0f);
    EXPECT_FLOAT_EQ(text->color.g, 0.0f);
    EXPECT_FLOAT_EQ(text->color.b, 0.0f);
    const PaintCall* font = painter.find("font");
    ASSERT_NE(font, nullptr);
    EXPECT_FLOAT_EQ(font->font_size, 24.0f);
    EXPECT_EQ(font->font, engine::builtin::font_ui);
    EXPECT_FLOAT_EQ(text->position.x, 0.0f);
    EXPECT_FLOAT_EQ(text->position.y, 0.0f);
    EXPECT_EQ(text->horizontal, engine::ui::UiAlign::Start);
    EXPECT_EQ(text->vertical, engine::ui::UiAlign::Start);
}

TEST(UiPainter, FontFamilyGuidFromCss) {
    TitleVm vm;
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Label class="title" text="{binding title}"/></Canvas>)", nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm).has_value());

    constexpr engine::AssetId hud_font{"c1a1c2d3e4f5678901234567890abc07"};
    const engine::ui::Stylesheet sheet =
            must_parse_css(".title { color: #ff0000; font-size: 24; font-family: c1a1c2d3e4f5678901234567890abc07; }");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    const PaintCall* font = painter.find("font");
    ASSERT_NE(font, nullptr);
    EXPECT_FLOAT_EQ(font->font_size, 24.0f);
    EXPECT_EQ(font->font, hud_font);
    EXPECT_NE(font->font, engine::builtin::font_ui);
}

TEST(UiPainter, PaddingInsetsLabelText) {
    TitleVm vm;
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Label class="title" text="{binding title}"/></Canvas>)", nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm).has_value());

    const engine::ui::Stylesheet sheet = must_parse_css(".title { padding: 8 12; }");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    const PaintCall* text = painter.find("text");
    ASSERT_NE(text, nullptr);
    EXPECT_FLOAT_EQ(text->position.x, 12.0f);
    EXPECT_FLOAT_EQ(text->position.y, 8.0f);
    EXPECT_EQ(text->horizontal, engine::ui::UiAlign::Start);
    EXPECT_EQ(text->vertical, engine::ui::UiAlign::Start);
}

TEST(UiPainter, JustifyAndAlignCenterText) {
    TitleVm vm;
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Label class="title" text="{binding title}"/></Canvas>)", nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm).has_value());

    const engine::ui::Stylesheet sheet =
            must_parse_css(".title { width: 200; height: 100; text-align: center; align-items: center; }");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    const PaintCall* text = painter.find("text");
    ASSERT_NE(text, nullptr);
    EXPECT_FLOAT_EQ(text->position.x, 100.0f);
    EXPECT_FLOAT_EQ(text->position.y, 50.0f);
    EXPECT_EQ(text->horizontal, engine::ui::UiAlign::Center);
    EXPECT_EQ(text->vertical, engine::ui::UiAlign::Center);
}

TEST(UiPainter, RelativePositionOffsetsOwnRectWithoutReflowingSiblings) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack class="hud" direction="vertical">
            <Label class="a" text="A"/>
            <Label class="b" text="B"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        .a { position: relative; top: 5; left: 3; background: #ffffff; }
        .b { background: #ffffff; }
    )");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    // z_index is untouched (both default to 0), so paint order stays document order: "A"'s
    // fill_rect comes before "B"'s.
    ASSERT_EQ(painter.count("fill_rect"), 2);
    std::vector<engine::render::Rect> fills;
    for (const PaintCall& call : painter.calls) {
        if (call.op == "fill_rect") {
            fills.push_back(call.rect);
        }
    }
    ASSERT_EQ(fills.size(), 2u);
    const engine::render::Rect& first = fills[0];
    const engine::render::Rect& second = fills[1];

    // A: normally {0,0,8,16} (1-char label, fake font size 16) - offset by top:5 left:3.
    EXPECT_FLOAT_EQ(first.x, 3.0f);
    EXPECT_FLOAT_EQ(first.y, 5.0f);
    // B: unaffected by A's offset - still stacked directly below A's *unoffset* position.
    EXPECT_FLOAT_EQ(second.x, 0.0f);
    EXPECT_FLOAT_EQ(second.y, 16.0f);
}

TEST(UiPainter, AbsoluteChildDoesNotConsumeFlowSpace) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack class="hud" direction="vertical">
            <Label class="badge" text="B"/>
            <Label class="item" text="X"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        .badge { position: absolute; top: 50; left: 60; background: #ffffff; }
        .item { background: #ffffff; }
    )");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    std::vector<engine::render::Rect> fills;
    for (const PaintCall& call : painter.calls) {
        if (call.op == "fill_rect") {
            fills.push_back(call.rect);
        }
    }
    ASSERT_EQ(fills.size(), 2u);
    // "badge" is absolutely positioned against the canvas root (no positioned ancestor).
    EXPECT_FLOAT_EQ(fills[0].x, 60.0f);
    EXPECT_FLOAT_EQ(fills[0].y, 50.0f);
    // "item" is the only *flow* child, so it starts the stack at (0,0) - badge reserved no space.
    EXPECT_FLOAT_EQ(fills[1].x, 0.0f);
    EXPECT_FLOAT_EQ(fills[1].y, 0.0f);
}

TEST(UiPainter, AbsoluteChildStretchesWhenOppositeInsetsSetAndNoExplicitSize) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Label class="stretch" text="S"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet =
            must_parse_css(".stretch { position: absolute; top: 10; right: 20; bottom: 10; left: 5; "
                            "background: #ffffff; }");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    const PaintCall* fill = painter.find("fill_rect");
    ASSERT_NE(fill, nullptr);
    EXPECT_FLOAT_EQ(fill->rect.x, 5.0f);
    EXPECT_FLOAT_EQ(fill->rect.y, 10.0f);
    EXPECT_FLOAT_EQ(fill->rect.w, 175.0f);  // 200 - 5(left) - 20(right)
    EXPECT_FLOAT_EQ(fill->rect.h, 80.0f);   // 100 - 10(top) - 10(bottom)
}

TEST(UiPainter, AbsoluteChildResolvesAgainstNearestPositionedAncestorNotRoot) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack class="outer" direction="vertical">
            <Label class="spacer" text="S"/>
            <Stack class="panel">
              <Label class="badge" text="B"/>
            </Stack>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        .panel { position: relative; width: 50; height: 40; }
        .badge { position: absolute; top: 2; left: 3; background: #ffffff; }
    )");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    const PaintCall* fill = painter.find("fill_rect");
    ASSERT_NE(fill, nullptr);
    // spacer (16px tall, fake font size) pushes .panel to y=16; badge = panel's box (0,16,50,40)
    // + (left:3, top:2), NOT the canvas root (0,0) + (3,2) it would land at if the containing
    // block were resolved wrong.
    EXPECT_FLOAT_EQ(fill->rect.x, 3.0f);
    EXPECT_FLOAT_EQ(fill->rect.y, 18.0f);
}

TEST(UiPainter, TransformFiresWithCenterRotationAndScale) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Label class="spin" text="S"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(
            ".spin { width: 20; height: 10; transform: rotate(90) scale(2); background: #fff; }");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    const PaintCall* transform = painter.find("transform");
    ASSERT_NE(transform, nullptr);
    // Label sits at (0,0,20,10) - the transform center is the rect's own center.
    EXPECT_FLOAT_EQ(transform->transform_center.x, 10.0f);
    EXPECT_FLOAT_EQ(transform->transform_center.y, 5.0f);
    EXPECT_NEAR(transform->rotation_radians, 1.5707964f, 1e-5f);  // 90deg
    EXPECT_FLOAT_EQ(transform->transform_scale, 2.0f);
}

TEST(UiPainter, ScissorAppliesAfterTransformSoRotatedContentIsNotClipped) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Label class="spin" text="S"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(
            ".spin { width: 20; height: 10; transform: rotate(90) scale(2); background: #fff; }");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    std::size_t transform_index = painter.calls.size();
    for (std::size_t i = 0; i < painter.calls.size(); ++i) {
        if (painter.calls[i].op == "transform") {
            transform_index = i;
            break;
        }
    }
    ASSERT_LT(transform_index, painter.calls.size());
    ASSERT_LT(transform_index + 1, painter.calls.size());
    // nvgScissor() bakes in whatever transform is active when called; setting it before the
    // rotation would clip this rotated element against its un-rotated axis-aligned rect instead.
    // The Canvas root's own (untransformed) scissor call precedes this, so check the call
    // immediately after the rotated element's transform, not just the first scissor anywhere.
    EXPECT_EQ(painter.calls[transform_index + 1].op, "scissor");
}

TEST(UiPainter, NoTransformDeclarationMeansNoTransformCall) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Label class="plain" text="S"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(".plain { color: #ffffff; }");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    EXPECT_EQ(painter.count("transform"), 0);
}

TEST(UiPainter, StackPaddingInsetsEqualSplit) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack class="hud" direction="vertical">
            <Label class="cell" text="A"/>
            <Label class="cell" text="B"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        .hud { padding: 10; }
        .cell { background: #ffffff; }
    )");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 100.f, 100.f}});

    std::vector<engine::render::Rect> fills;
    for (const PaintCall& call : painter.calls) {
        if (call.op == "fill_rect") {
            fills.push_back(call.rect);
        }
    }
    ASSERT_EQ(fills.size(), 2u);
    const float cell_w = fake_text_width("A");
    const float cell_h = kFakeFontSize;
    EXPECT_FLOAT_EQ(fills[0].x, 10.0f);
    EXPECT_FLOAT_EQ(fills[0].y, 10.0f);
    EXPECT_FLOAT_EQ(fills[0].w, cell_w);
    EXPECT_FLOAT_EQ(fills[0].h, cell_h);
    EXPECT_FLOAT_EQ(fills[1].x, 10.0f);
    EXPECT_FLOAT_EQ(fills[1].y, 10.0f + cell_h);
    EXPECT_FLOAT_EQ(fills[1].w, cell_w);
    EXPECT_FLOAT_EQ(fills[1].h, cell_h);
}

TEST(UiPainter, ButtonHoverUsesPseudoBackground) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Button class="cell" content="X"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        Button { width: 100; height: 100; background: #111111; }
        Button:hover { background: #333333; }
    )");

    FakePainter idle;
    engine::ui::paint_document(*parsed, &sheet, idle,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 100.f, 100.f}, .pointer = {1000.f, 1000.f}});
    const PaintCall* idle_fill = idle.find("fill_rect");
    ASSERT_NE(idle_fill, nullptr);
    EXPECT_NEAR(idle_fill->color.r, 0x11 / 255.0f, 0.01f);

    FakePainter hover;
    engine::ui::paint_document(*parsed, &sheet, hover,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 100.f, 100.f}, .pointer = {10.f, 10.f}});
    const PaintCall* hover_fill = hover.find("fill_rect");
    ASSERT_NE(hover_fill, nullptr);
    EXPECT_NEAR(hover_fill->color.r, 0x33 / 255.0f, 0.01f);
}

TEST(UiPainter, ButtonHoverUsesPseudoBackgroundImage) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Button class="cell" content="X"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        Button { width: 100; height: 100; }
        Button:hover { background-image: c1a1c2d3e4f5678901234567890abc0a; }
    )");

    FakePainter idle;
    engine::ui::paint_document(*parsed, &sheet, idle,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 100.f, 100.f}, .pointer = {1000.f, 1000.f}});
    EXPECT_EQ(find_image(idle, kHoverImage), nullptr);
    EXPECT_EQ(idle.count("image"), 0);

    FakePainter hover;
    engine::ui::paint_document(*parsed, &sheet, hover,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 100.f, 100.f}, .pointer = {10.f, 10.f}});
    const engine::ui::Element* button = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::Button);
    ASSERT_NE(button, nullptr);
    const PaintCall* image = find_image(hover, kHoverImage);
    ASSERT_NE(image, nullptr);
    EXPECT_FLOAT_EQ(image->rect.x, button->layout_rect.x);
    EXPECT_FLOAT_EQ(image->rect.y, button->layout_rect.y);
    EXPECT_FLOAT_EQ(image->rect.w, button->layout_rect.w);
    EXPECT_FLOAT_EQ(image->rect.h, button->layout_rect.h);
}

TEST(UiPainter, BackgroundImageNoneSkipsPaint) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Button content="X"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        Button { width: 80; height: 40; background-image: c1a1c2d3e4f5678901234567890abc0a; }
        Button:hover { background-image: none; }
    )");

    FakePainter idle;
    engine::ui::paint_document(*parsed, &sheet, idle,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 80.f, 40.f}, .pointer = {1000.f, 1000.f}});
    ASSERT_NE(find_image(idle, kHoverImage), nullptr);

    FakePainter hover;
    engine::ui::paint_document(*parsed, &sheet, hover,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 80.f, 40.f}, .pointer = {10.f, 10.f}});
    EXPECT_EQ(find_image(hover, kHoverImage), nullptr);
    EXPECT_EQ(hover.count("image"), 0);
}

TEST(UiPainter, BackgroundImageFilenameDoesNotPaint) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Button content="X"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(R"(
        Button { width: 80; height: 40; background-image: hover.png; }
    )",
            warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_TRUE(!warnings.empty());
    bool mentions_filename = false;
    for (const std::string& warning : warnings) {
        if (warning.find("hover.png") != std::string::npos) {
            mentions_filename = true;
        }
    }
    EXPECT_TRUE(mentions_filename);

    FakePainter painter;
    engine::ui::paint_document(*parsed, &*sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 80.f, 40.f}, .pointer = {10.f, 10.f}});
    EXPECT_EQ(painter.count("image"), 0);
}

TEST(UiPainter, DisabledButtonAppliesOpacity) {
    ToggleVm vm;
    vm.go.set_can_execute(false);
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Button command="{binding go}" content="Go"/></Canvas>)", nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm).has_value());
    const engine::ui::Element* button = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::Button);
    ASSERT_NE(button, nullptr);
    EXPECT_TRUE(button->disabled);

    const engine::ui::Stylesheet sheet = must_parse_css("Button:disabled { opacity: 0.5; background: #ffffff; }");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 80.f, 40.f}});

    bool saw_disabled_opacity = false;
    for (const PaintCall& call : painter.calls) {
        if (call.op == "opacity" && call.opacity == 0.5f) {
            saw_disabled_opacity = true;
        }
    }
    EXPECT_TRUE(saw_disabled_opacity);
}

TEST(UiPainter, ItemsControlPaintsItemDataContext) {
    BoardVm board;
    auto a = std::make_shared<CellVm>();
    a->mark.set("X");
    auto b = std::make_shared<CellVm>();
    b->mark.set("O");
    board.cells.set({a, b});

    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <ItemsControl items_source="{binding cells}">
            <ItemTemplate>
              <Button class="cell" command="{binding click}" content="{binding mark}"/>
            </ItemTemplate>
          </ItemsControl>
        </Canvas>
    )",
            nullptr, &board);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, board).has_value());

    const engine::ui::Element* items =
            engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    ASSERT_EQ(items->generated_items.size(), 2u);
    EXPECT_EQ(items->generated_items[0].text, "X");
    EXPECT_EQ(items->generated_items[1].text, "O");

    FakePainter painter;
    engine::ui::paint_document(*parsed, nullptr, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 80.f}});

    std::vector<std::string> texts;
    for (const PaintCall& call : painter.calls) {
        if (call.op == "text") {
            texts.push_back(call.text);
        }
    }
    ASSERT_EQ(texts.size(), 2u);
    EXPECT_EQ(texts[0], "X");
    EXPECT_EQ(texts[1], "O");
}

TEST(UiPainter, ImageSourceFromBoundAssetId) {
    IconVm vm{kIconGuid};
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Image source="{binding icon}"/></Canvas>)", nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm).has_value());

    const engine::ui::Element* image = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::Image);
    ASSERT_NE(image, nullptr);
    ASSERT_TRUE(image->source.has_value());
    EXPECT_EQ(*image->source, kIconGuid);

    FakePainter painter;
    const engine::render::Rect canvas{0.f, 0.f, 200.f, 100.f};
    engine::ui::paint_document(*parsed, nullptr, painter, engine::ui::UiPaintInput{.canvas_rect = canvas});

    const PaintCall* call = painter.find("image");
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->texture, kIconGuid);
    EXPECT_FLOAT_EQ(call->rect.x, 0.0f);
    EXPECT_FLOAT_EQ(call->rect.y, 0.0f);
    EXPECT_FLOAT_EQ(call->rect.w, kDefaultImageSize);
    EXPECT_FLOAT_EQ(call->rect.h, kDefaultImageSize);
}

TEST(UiPainter, ImageSourceLiteralGuidPaints) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Image source="c1a1c2d3e4f5678901234567890abc09"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());

    const engine::ui::Element* image = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::Image);
    ASSERT_NE(image, nullptr);
    ASSERT_TRUE(image->source.has_value());
    EXPECT_EQ(*image->source, kIconGuid);

    FakePainter painter;
    const engine::render::Rect canvas{0.f, 0.f, 64.f, 32.f};
    engine::ui::paint_document(*parsed, nullptr, painter, engine::ui::UiPaintInput{.canvas_rect = canvas});

    const PaintCall* call = painter.find("image");
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->texture, kIconGuid);
    EXPECT_FLOAT_EQ(call->rect.x, 0.0f);
    EXPECT_FLOAT_EQ(call->rect.y, 0.0f);
    EXPECT_FLOAT_EQ(call->rect.w, kDefaultImageSize);
    EXPECT_FLOAT_EQ(call->rect.h, kDefaultImageSize);
}

TEST(UiPainter, ImageSourceStringBindingFailsAtApply) {
    StringIconVm vm{std::string(kIconGuid.hex())};
    RecordingFatalError fatal;
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Image source="{binding icon}"/></Canvas>)", nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());

    const auto applied = engine::ui::apply_bindings(*parsed, vm, &fatal);
    EXPECT_FALSE(applied.has_value());
    EXPECT_EQ(applied.error(), engine::ui::UiError::MissingBinding);
    EXPECT_GE(fatal.call_count, 1);

    const engine::ui::Element* image = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::Image);
    ASSERT_NE(image, nullptr);
    EXPECT_FALSE(image->source.has_value());
}

TEST(UiPainter, LabelsHugDifferentTextWidths) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack>
            <Label class="cell" text="Hi"/>
            <Label class="cell" text="Hello"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(".cell { background: #ffffff; }");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    std::vector<engine::render::Rect> fills;
    for (const PaintCall& call : painter.calls) {
        if (call.op == "fill_rect") {
            fills.push_back(call.rect);
        }
    }
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_FLOAT_EQ(fills[0].w, fake_text_width("Hi"));
    EXPECT_FLOAT_EQ(fills[1].w, fake_text_width("Hello"));
    EXPECT_GT(fills[1].w, fills[0].w);
    EXPECT_FLOAT_EQ(fills[0].h, kFakeFontSize);
    EXPECT_FLOAT_EQ(fills[1].h, kFakeFontSize);
}

TEST(UiPainter, WidthClampsUsedSize) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Label class="title" text="HelloWorld!"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(".title { width: 80; background: #ffffff; }");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    const PaintCall* fill = painter.find("fill_rect");
    ASSERT_NE(fill, nullptr);
    EXPECT_GT(fake_text_width("HelloWorld!"), 80.0f);
    EXPECT_FLOAT_EQ(fill->rect.w, 80.0f);
    EXPECT_FLOAT_EQ(fill->rect.h, kFakeFontSize);
}

TEST(UiPainter, MarginOffsetsSiblingRect) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack>
            <Label class="a" text="A"/>
            <Label class="b" text="B"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        .a { background: #ffffff; }
        .b { background: #ffffff; margin: 10; }
    )");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    std::vector<engine::render::Rect> fills;
    for (const PaintCall& call : painter.calls) {
        if (call.op == "fill_rect") {
            fills.push_back(call.rect);
        }
    }
    ASSERT_EQ(fills.size(), 2u);
    EXPECT_FLOAT_EQ(fills[0].x, 0.0f);
    EXPECT_FLOAT_EQ(fills[0].y, 0.0f);
    EXPECT_FLOAT_EQ(fills[1].x, 10.0f);
    EXPECT_FLOAT_EQ(fills[1].y, fills[0].y + fills[0].h + 10.0f);
}

TEST(UiPainter, StackJustifyContentMovesChildren) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack class="col">
            <Label class="cell" text="A"/>
            <Label class="cell" text="B"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        .col { width: 40; height: 100; justify-content: center; }
        .cell { background: #ffffff; }
    )");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    std::vector<engine::render::Rect> fills;
    std::vector<glm::vec2> texts;
    for (const PaintCall& call : painter.calls) {
        if (call.op == "fill_rect") {
            fills.push_back(call.rect);
        }
        if (call.op == "text") {
            texts.push_back(call.position);
        }
    }
    ASSERT_EQ(fills.size(), 2u);
    ASSERT_EQ(texts.size(), 2u);
    const float packed = kFakeFontSize * 2.0f;
    const float offset = (100.0f - packed) * 0.5f;
    EXPECT_FLOAT_EQ(fills[0].y, offset);
    EXPECT_FLOAT_EQ(fills[1].y, offset + kFakeFontSize);
    EXPECT_FLOAT_EQ(texts[0].y, fills[0].y);
    EXPECT_FLOAT_EQ(texts[1].y, fills[1].y);
    EXPECT_NE(texts[0].y, 50.0f);
}

TEST(UiPainter, TextAlignCenterMovesGlyphs) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Label class="title" text="Hi"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet =
            must_parse_css(".title { width: 200; height: 100; text-align: center; align-items: center; }");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    const PaintCall* text = painter.find("text");
    ASSERT_NE(text, nullptr);
    EXPECT_FLOAT_EQ(text->position.x, 100.0f);
    EXPECT_FLOAT_EQ(text->position.y, 50.0f);
    EXPECT_EQ(text->horizontal, engine::ui::UiAlign::Center);
    EXPECT_EQ(text->vertical, engine::ui::UiAlign::Center);
}

TEST(UiPainter, LaterStylesheetWinsAtEqualSpecificity) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Button content="Go"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    engine::ui::Stylesheet merged =
            must_parse_css("Button { width: 80; height: 40; background: #ff0000; }");
    const engine::ui::Stylesheet later = must_parse_css("Button { background: #00ff00; }");
    merged.rules.insert(merged.rules.end(), later.rules.begin(), later.rules.end());

    FakePainter painter;
    engine::ui::paint_document(*parsed, &merged, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 80.f, 40.f}});
    const PaintCall* fill = painter.find("fill_rect");
    ASSERT_NE(fill, nullptr);
    EXPECT_NEAR(fill->color.r, 0.0f, 0.01f);
    EXPECT_NEAR(fill->color.g, 1.0f, 0.01f);
    EXPECT_NEAR(fill->color.b, 0.0f, 0.01f);
}

[[nodiscard]] const engine::ui::Element* find_class(const engine::ui::Element& root, std::string_view class_name) {
    if (root.class_name == class_name) {
        return &root;
    }
    for (const engine::ui::Element& child : root.children) {
        if (const engine::ui::Element* found = find_class(child, class_name)) {
            return found;
        }
    }
    return nullptr;
}

[[nodiscard]] const PaintCall* find_fill_at(const FakePainter& painter, const engine::render::Rect& rect) {
    for (const PaintCall& call : painter.calls) {
        if (call.op == "fill_rect" && call.rect.x == rect.x && call.rect.y == rect.y && call.rect.w == rect.w &&
                call.rect.h == rect.h) {
            return &call;
        }
    }
    return nullptr;
}

TEST(UiPainter, DescendantSelectorMatchesInsideStackHud) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack>
            <Stack class="hud">
              <Label class="inside" text="In"/>
              <Stack class="inner">
                <Label class="nested" text="Mid"/>
              </Stack>
            </Stack>
            <Label class="outside" text="Out"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        Label { width: 40; height: 16; background: #00ff00; }
        Stack.hud Label { background: #ff0000; }
    )");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    const engine::ui::Element* inside = find_class(parsed->root, "inside");
    const engine::ui::Element* nested = find_class(parsed->root, "nested");
    const engine::ui::Element* outside = find_class(parsed->root, "outside");
    ASSERT_NE(inside, nullptr);
    ASSERT_NE(nested, nullptr);
    ASSERT_NE(outside, nullptr);
    const PaintCall* inside_fill = find_fill_at(painter, inside->layout_rect);
    const PaintCall* nested_fill = find_fill_at(painter, nested->layout_rect);
    const PaintCall* outside_fill = find_fill_at(painter, outside->layout_rect);
    ASSERT_NE(inside_fill, nullptr);
    ASSERT_NE(nested_fill, nullptr);
    ASSERT_NE(outside_fill, nullptr);
    EXPECT_NEAR(inside_fill->color.r, 1.0f, 0.01f);
    EXPECT_NEAR(nested_fill->color.r, 1.0f, 0.01f);
    EXPECT_NEAR(outside_fill->color.r, 0.0f, 0.01f);
    EXPECT_NEAR(outside_fill->color.g, 1.0f, 0.01f);
}

TEST(UiPainter, ChildSelectorMatchesDirectChildOnly) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack class="hud">
            <Label class="direct" text="A"/>
            <Stack class="inner">
              <Label class="nested" text="B"/>
            </Stack>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        Label { width: 40; height: 16; background: #00ff00; }
        Stack.hud > Label { background: #ff0000; }
    )");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    const engine::ui::Element* direct = find_class(parsed->root, "direct");
    const engine::ui::Element* nested = find_class(parsed->root, "nested");
    ASSERT_NE(direct, nullptr);
    ASSERT_NE(nested, nullptr);
    const PaintCall* direct_fill = find_fill_at(painter, direct->layout_rect);
    const PaintCall* nested_fill = find_fill_at(painter, nested->layout_rect);
    ASSERT_NE(direct_fill, nullptr);
    ASSERT_NE(nested_fill, nullptr);
    EXPECT_NEAR(direct_fill->color.r, 1.0f, 0.01f);
    EXPECT_NEAR(direct_fill->color.g, 0.0f, 0.01f);
    EXPECT_NEAR(nested_fill->color.r, 0.0f, 0.01f);
    EXPECT_NEAR(nested_fill->color.g, 1.0f, 0.01f);
}

TEST(UiPainter, WidthPercentOfParentContentBox) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack class="box">
            <Label class="half" text="Hi"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        .box { width: 220; height: 80; padding: 10; background: #000000; }
        .half { width: 50%; height: 20; background: #ff0000; }
    )");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 400.f, 200.f}});

    const engine::ui::Element* half = find_class(parsed->root, "half");
    ASSERT_NE(half, nullptr);
    EXPECT_FLOAT_EQ(half->layout_rect.w, 100.0f);
    const PaintCall* fill = find_fill_at(painter, half->layout_rect);
    ASSERT_NE(fill, nullptr);
    EXPECT_FLOAT_EQ(fill->rect.w, 100.0f);
    EXPECT_NEAR(fill->color.r, 1.0f, 0.01f);
}

TEST(UiPainter, PaddingEmUsesElementFontSize) {
    TitleVm vm;
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Label class="title" text="{binding title}"/></Canvas>)", nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm).has_value());

    const engine::ui::Stylesheet sheet = must_parse_css(".title { font-size: 20; padding: 1em; }");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    const PaintCall* text = painter.find("text");
    ASSERT_NE(text, nullptr);
    EXPECT_FLOAT_EQ(text->position.x, 20.0f);
    EXPECT_FLOAT_EQ(text->position.y, 20.0f);
}

TEST(UiPainter, CalcWidthSubtractsFromParentContentBox) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Stack class="box">
            <Label class="fit" text="Hi"/>
          </Stack>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        .box { width: 220; height: 80; padding: 10; background: #000000; }
        .fit { width: calc(100% - 16); height: 20; background: #ff0000; }
    )");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 400.f, 200.f}});

    const engine::ui::Element* fit = find_class(parsed->root, "fit");
    ASSERT_NE(fit, nullptr);
    EXPECT_FLOAT_EQ(fit->layout_rect.w, 184.0f);
    const PaintCall* fill = find_fill_at(painter, fit->layout_rect);
    ASSERT_NE(fill, nullptr);
    EXPECT_FLOAT_EQ(fill->rect.w, 184.0f);
}

TEST(UiPainter, MediaMinWidthAppliesAfterResize) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Label class="title" text="Hi"/>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        .title { height: 20; background: #ff0000; }
        @media (min-width: 800) { .title { width: 200; } }
    )");

    FakePainter narrow;
    engine::ui::paint_document(*parsed, &sheet, narrow,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 400.f, 200.f}, .window_width = 799.f, .window_height = 600.f});
    const engine::ui::Element* title = find_class(parsed->root, "title");
    ASSERT_NE(title, nullptr);
    EXPECT_NE(title->layout_rect.w, 200.0f);

    FakePainter wide;
    engine::ui::paint_document(*parsed, &sheet, wide,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 400.f, 200.f}, .window_width = 800.f, .window_height = 600.f});
    title = find_class(parsed->root, "title");
    ASSERT_NE(title, nullptr);
    EXPECT_FLOAT_EQ(title->layout_rect.w, 200.0f);
    const PaintCall* fill = find_fill_at(wide, title->layout_rect);
    ASSERT_NE(fill, nullptr);
    EXPECT_FLOAT_EQ(fill->rect.w, 200.0f);
}

TEST(UiPainter, ScaleWithScreenSizeTransformsRectAndFontSize) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Button content="Go"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet =
            must_parse_css("Button { width: 100; height: 40; font-size: 16; background: #ffffff; }");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{
                    .canvas_rect = {0.f, 0.f, 200.f, 100.f}, .ui_offset = {50.f, 25.f}, .ui_scale = 2.0f});

    const PaintCall* fill = painter.find("fill_rect");
    ASSERT_NE(fill, nullptr);
    EXPECT_FLOAT_EQ(fill->rect.x, 50.0f);
    EXPECT_FLOAT_EQ(fill->rect.y, 25.0f);
    EXPECT_FLOAT_EQ(fill->rect.w, 200.0f);
    EXPECT_FLOAT_EQ(fill->rect.h, 80.0f);

    const PaintCall* font = painter.find("font");
    ASSERT_NE(font, nullptr);
    EXPECT_FLOAT_EQ(font->font_size, 32.0f);

    const PaintCall* text = painter.find("text");
    ASSERT_NE(text, nullptr);
    EXPECT_FLOAT_EQ(text->position.x, 50.0f);
    EXPECT_FLOAT_EQ(text->position.y, 25.0f);
}

TEST(UiPainter, IdentityTransformLeavesRectUnchanged) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Button content="Go"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css("Button { width: 100; height: 40; background: #ffffff; }");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    const PaintCall* fill = painter.find("fill_rect");
    ASSERT_NE(fill, nullptr);
    EXPECT_FLOAT_EQ(fill->rect.x, 0.0f);
    EXPECT_FLOAT_EQ(fill->rect.y, 0.0f);
    EXPECT_FLOAT_EQ(fill->rect.w, 100.0f);
    EXPECT_FLOAT_EQ(fill->rect.h, 40.0f);
}

TEST(UiPainter, KeyframeOpacityAdvancesWithDeltaTime) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Label class="fade" text="Hi"/>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        @keyframes fade {
            from { opacity: 0; }
            to { opacity: 1; }
        }
        .fade { animation-name: fade; animation-duration: 1s; }
    )");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}, .delta_time = 0.5f});

    const PaintCall* fade = nullptr;
    for (const PaintCall& call : painter.calls) {
        if (call.op == "opacity" && call.opacity < 0.99f) {
            fade = &call;
            break;
        }
    }
    ASSERT_NE(fade, nullptr);
    EXPECT_NEAR(fade->opacity, 0.5f, 0.01f);
}

TEST(UiPainter, ChildStackingOrderSortsByZIndexStableOnTies) {
    std::vector<engine::ui::Element> children(4);
    children[0].name = "a";
    children[0].z_index = 0;
    children[1].name = "b";
    children[1].z_index = -1;
    children[2].name = "c";
    children[2].z_index = 2;
    children[3].name = "d";
    children[3].z_index = 0;

    const std::vector<engine::ui::Element*> order = engine::ui::child_stacking_order(children);
    ASSERT_EQ(order.size(), 4u);
    // ascending z-index (-1, 0, 0, 2); the two z_index==0 siblings ("a" then "d") keep their
    // original relative (document) order — this is the stable-sort tie-break the whole z-index
    // feature relies on to leave untouched (all-zero) trees unchanged.
    EXPECT_EQ(order[0]->name, "b");
    EXPECT_EQ(order[1]->name, "a");
    EXPECT_EQ(order[2]->name, "d");
    EXPECT_EQ(order[3]->name, "c");
}

TEST(UiPainter, ZIndexOverridesDocumentOrderAtPaintTime) {
    // "back" is first in the XML (would paint last / on top under today's document-order-only
    // rule) but gets a lower z-index via CSS, so it must paint FIRST (behind) despite that.
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Label class="back" text="Back"/>
          <Label class="front" text="Front"/>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(".back { z-index: -1; } .front { z-index: 5; }");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}});

    int back_index = -1;
    int front_index = -1;
    for (std::size_t i = 0; i < painter.calls.size(); ++i) {
        const PaintCall& call = painter.calls[i];
        if (call.op != "text") {
            continue;
        }
        if (call.text == "Back") {
            back_index = static_cast<int>(i);
        } else if (call.text == "Front") {
            front_index = static_cast<int>(i);
        }
    }
    ASSERT_NE(back_index, -1);
    ASSERT_NE(front_index, -1);
    EXPECT_LT(back_index, front_index);
}

TEST(UiPainter, OnlyTopmostOverlappingButtonGetsHovered) {
    auto parsed = engine::ui::parse_xml(R"(
        <Canvas>
          <Button class="back" content="Back"/>
          <Button class="front" content="Front"/>
        </Canvas>
    )");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(".back { z-index: 5; } .front { z-index: 1; }");
    FakePainter painter;
    engine::ui::paint_document(*parsed, &sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = {0.f, 0.f, 200.f, 100.f}, .pointer = {5.f, 5.f}});

    ASSERT_EQ(parsed->root.children.size(), 2u);
    const engine::ui::Element& back = parsed->root.children[0];
    const engine::ui::Element& front = parsed->root.children[1];
    ASSERT_EQ(back.class_name, "back");
    ASSERT_EQ(front.class_name, "front");
    // "back" has the higher z-index, so it's the topmost element at (5,5) despite being first
    // in document order - it alone gets :hover, "front" does not (they geometrically overlap).
    EXPECT_TRUE(back.hovered);
    EXPECT_FALSE(front.hovered);
}
