#include <gtest/gtest.h>

#include "ui/painter.h"

#include <engine/builtin_ids.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <memory>
#include <string>
#include <string_view>
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
    std::string text;
    glm::vec2 position{};
    engine::ui::UiAlign horizontal = engine::ui::UiAlign::Start;
    engine::ui::UiAlign vertical = engine::ui::UiAlign::Start;
};

class FakePainter final : public engine::ui::IUiPainter {
public:
    std::vector<PaintCall> calls;

    void save() override { calls.push_back(PaintCall{.op = "save"}); }

    void restore() override { calls.push_back(PaintCall{.op = "restore"}); }

    void scissor(const engine::render::Rect& rect) override {
        calls.push_back(PaintCall{.op = "scissor", .rect = rect});
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
        calls.push_back(PaintCall{.op = "image", .rect = rect, .font = texture});
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

    TitleVm() { property("title", title); }
};

class CellVm final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<std::string> mark;
    engine::ui::RelayCommand click;

    CellVm() {
        property("mark", mark);
        command("click", click);
    }
};

class BoardVm final : public engine::ui::ViewModel {
public:
    engine::ui::BindableList<std::shared_ptr<CellVm>> cells;

    BoardVm() { property("cells", cells); }
};

class ToggleVm final : public engine::ui::ViewModel {
public:
    engine::ui::RelayCommand go;
    bool enabled = true;

    ToggleVm() {
        command("go", go);
        go.set_can_execute(true);
        go = [] {};
    }
};

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
            must_parse_css(".title { justify-content: center; align-items: center; }");
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
    EXPECT_FLOAT_EQ(fills[0].x, 10.0f);
    EXPECT_FLOAT_EQ(fills[0].y, 10.0f);
    EXPECT_FLOAT_EQ(fills[0].w, 80.0f);
    EXPECT_FLOAT_EQ(fills[0].h, 40.0f);
    EXPECT_FLOAT_EQ(fills[1].x, 10.0f);
    EXPECT_FLOAT_EQ(fills[1].y, 50.0f);
    EXPECT_FLOAT_EQ(fills[1].w, 80.0f);
    EXPECT_FLOAT_EQ(fills[1].h, 40.0f);
}

TEST(UiPainter, ButtonHoverUsesPseudoBackground) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Button class="cell" content="X"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = must_parse_css(R"(
        Button { background: #111111; }
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
