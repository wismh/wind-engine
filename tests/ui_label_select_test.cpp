#include <gtest/gtest.h>

#include "ui/painter.h"
#include "ui/text_select.h"

#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

class FakePainter final : public engine::ui::IUiPainter {
public:
    std::vector<engine::render::Rect> rects;

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

class CommandViewModel final : public engine::ui::ViewModel {
public:
    int runs = 0;
    engine::ui::RelayCommand go;

    CommandViewModel() {
        command(engine::ui::intern("go"), go);
        go = [this] { ++runs; };
    }
};

struct Fixture {
    engine::ecs::World world;
    FakePainter painter;
    engine::ecs::Entity entity{};
    std::shared_ptr<std::string> clipboard = std::make_shared<std::string>();
    std::shared_ptr<engine::ui::ViewModel> vm;

    engine::ui::Element& root() { return world.get<engine::ui::UiInstance>(entity).document.root; }

    void setup(std::string_view xml, std::string_view css, std::shared_ptr<engine::ui::ViewModel> data = nullptr) {
        vm = std::move(data);
        auto parsed = engine::ui::parse_xml(xml, nullptr, vm.get());
        EXPECT_TRUE(parsed.has_value());
        std::vector<std::string> warnings;
        auto sheet = engine::ui::parse_css(css, warnings);
        EXPECT_TRUE(sheet.has_value());

        engine::ui::UiCanvas canvas;
        canvas.rect = engine::render::Rect{0.0f, 0.0f, 400.0f, 400.0f};
        canvas.fit = engine::ui::UiFit::Fixed;
        canvas.data_context = vm;

        entity = world.create();
        world.emplace<engine::ui::UiCanvas>(entity, canvas);
        world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{std::move(*parsed), std::move(*sheet)});
        world.ctx<engine::ui::UiLayoutPainters>().resolve = [this](engine::WindowId) -> engine::ui::IUiPainter* {
            return &painter;
        };
        world.ctx<engine::ui::UiClipboard>() = engine::ui::UiClipboard{
                .set_text = [storage = clipboard](std::string_view text) { *storage = std::string(text); },
                .get_text = [storage = clipboard]() -> std::optional<std::string> { return *storage; },
        };
    }

    void paint() {
        auto& instance = world.get<engine::ui::UiInstance>(entity);
        const engine::ui::Stylesheet* sheet = instance.stylesheet ? &*instance.stylesheet : nullptr;
        engine::ui::paint_document(instance.document, sheet, painter,
                engine::ui::UiPaintInput{.canvas_rect = engine::render::Rect{0.0f, 0.0f, 400.0f, 400.0f}});
    }

    void click(float x, float y, std::uint8_t clicks = 1, bool primary = true) {
        engine::ui::begin_frame(world);
        engine::ui::handle_pointer(world, x, y, engine::kPrimaryWindow, primary, clicks);
    }
};

[[nodiscard]] std::string selected(const engine::ui::Element& element) {
    if (!element.selection_anchor || *element.selection_anchor == element.caret_position) {
        return {};
    }
    const std::size_t begin = std::min(*element.selection_anchor, element.caret_position);
    const std::size_t end = std::max(*element.selection_anchor, element.caret_position);
    return element.text.substr(begin, end - begin);
}

void press(engine::ecs::World& world, engine::KeyCode key, bool ctrl = false) {
    if (ctrl) {
        engine::ui::handle_key(world, engine::KeyCode::LCtrl, true);
    }
    engine::ui::handle_key(world, key, true);
    engine::ui::handle_key(world, key, false);
    if (ctrl) {
        engine::ui::handle_key(world, engine::KeyCode::LCtrl, false);
    }
}

constexpr std::string_view kSelectable = "Label { user-select: text; width: 200px; font-size: 16px; }";

}

TEST(LabelSelect, WordRangeSplitsWordsSpacesPunctuationAndAtomicCharacters) {
    EXPECT_EQ(engine::ui::word_range("hello", 0).end, 5u);
    EXPECT_EQ(engine::ui::word_range("hello world", 0).end, 5u);
    EXPECT_EQ(engine::ui::word_range("hello world", 6).begin, 6u);

    const engine::ui::TextRange hello_comma = engine::ui::word_range("hello,", 5);
    EXPECT_EQ(hello_comma.begin, 5u);
    EXPECT_EQ(hello_comma.end, 6u);
    EXPECT_EQ(engine::ui::word_range("hello,", 1).end, 5u);

    const engine::ui::TextRange spaces = engine::ui::word_range("a  b", 1);
    EXPECT_EQ(spaces.begin, 1u);
    EXPECT_EQ(spaces.end, 3u);

    EXPECT_EQ(engine::ui::word_range("well-known", 0).end, std::string("well-known").size());
    EXPECT_EQ(engine::ui::word_range("well-", 0).end, 4u);
    EXPECT_EQ(engine::ui::word_range("well-", 4).begin, 4u);

    const std::string meat = "м'ясо";
    const engine::ui::TextRange cyr = engine::ui::word_range(meat, 0);
    EXPECT_EQ(cyr.begin, 0u);
    EXPECT_EQ(cyr.end, meat.size());
    const std::string meat_apos = "м\u2019ясо";
    EXPECT_EQ(engine::ui::word_range(meat_apos, 2).end, meat_apos.size());

    const std::string greek = "αβ";
    EXPECT_EQ(engine::ui::word_range(greek, 0).end, greek.size());

    const std::string cjk = "字字";
    const engine::ui::TextRange first = engine::ui::word_range(cjk, 0);
    EXPECT_EQ(first.begin, 0u);
    EXPECT_EQ(first.end, 3u);
    EXPECT_EQ(engine::ui::word_range(cjk, 3).begin, 3u);

    EXPECT_EQ(engine::ui::word_range("!!", 1).end, 2u);
    EXPECT_EQ(engine::ui::word_range("", 0).end, 0u);
}

TEST(LabelSelect, UserSelectDefaultsToNoneAndUnknownValueStaysNone) {
    Fixture fx;
    fx.setup(R"(<Canvas><Label id="t" text="hello"/></Canvas>)", "Label { user-select: contain; width: 80px; }");
    fx.paint();
    EXPECT_EQ(fx.root().children[0].user_select, engine::ui::UserSelect::None);

    fx.click(4.0f, 4.0f);
    EXPECT_EQ(engine::ui::focused_element(fx.world), nullptr);
    EXPECT_FALSE(fx.world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
}

TEST(LabelSelect, AbsentUserSelectDoesNotConsumeTheClick) {
    Fixture fx;
    fx.setup(R"(<Canvas><Label id="t" text="hello"/></Canvas>)", "Label { width: 80px; font-size: 16px; }");
    fx.paint();
    fx.click(4.0f, 4.0f);
    EXPECT_EQ(engine::ui::focused_element(fx.world), nullptr);
    EXPECT_FALSE(fx.world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));
}

TEST(LabelSelect, DragSelectsAByteRangeOnTheClickedLine) {
    Fixture fx;
    fx.setup(R"(<Canvas><Label id="t" text="hello"/></Canvas>)", kSelectable);
    fx.paint();
    engine::ui::Element& label = fx.root().children[0];
    ASSERT_EQ(label.painted_text_lines.size(), 1u);
    const engine::ui::PaintedTextLine& line = label.painted_text_lines[0];

    fx.click(line.x + 1.0f, line.y + 1.0f);
    EXPECT_EQ(label.caret_position, 0u);
    EXPECT_EQ(*label.selection_anchor, 0u);
    EXPECT_TRUE(fx.world.ctx<engine::ui::MouseConsumed>().consumed_for(engine::kPrimaryWindow));

    engine::ui::pointer_for(fx.world, engine::kPrimaryWindow).down = true;
    engine::ui::update_text_selection(fx.world, line.x + 24.0f, line.y + 1.0f);
    EXPECT_EQ(selected(label), "hel");
}

TEST(LabelSelect, SecondWrappedLineIsAddressedByY) {
    Fixture fx;
    fx.setup(R"(<Canvas><Label id="t" text="hello&#10;world"/></Canvas>)", kSelectable);
    fx.paint();
    engine::ui::Element& label = fx.root().children[0];
    ASSERT_EQ(label.painted_text_lines.size(), 2u);
    const engine::ui::PaintedTextLine& first = label.painted_text_lines[0];
    const engine::ui::PaintedTextLine& second = label.painted_text_lines[1];
    EXPECT_EQ(first.begin, 0u);
    EXPECT_EQ(second.begin, 6u);

    fx.click(first.x + 1.0f, first.y + 1.0f);
    engine::ui::pointer_for(fx.world, engine::kPrimaryWindow).down = true;
    engine::ui::update_text_selection(fx.world, second.x + 1.0f, second.y + 1.0f);
    EXPECT_EQ(selected(label), "hello\n");
}

TEST(LabelSelect, CenterAlignUsesGlyphLeftNotTheContentCenter) {
    Fixture fx;
    fx.setup(R"(<Canvas><Label id="t" text="ab"/></Canvas>)",
            "Label { user-select: text; width: 200px; height: 40px; font-size: 16px; text-align: center; }");
    fx.paint();
    engine::ui::Element& label = fx.root().children[0];
    ASSERT_EQ(label.painted_text_lines.size(), 1u);
    const engine::ui::PaintedTextLine line = label.painted_text_lines[0];
    EXPECT_NEAR(line.x, 92.0f, 0.01f);
    EXPECT_NEAR(line.width, 16.0f, 0.01f);

    fx.click(line.x + 9.0f, line.y + 1.0f);
    EXPECT_EQ(label.caret_position, 1u);

    press(fx.world, engine::KeyCode::A, true);
    fx.painter.rects.clear();
    fx.paint();
    ASSERT_FALSE(fx.painter.rects.empty());
    EXPECT_NEAR(fx.painter.rects.back().x, line.x, 0.01f);
    EXPECT_NEAR(fx.painter.rects.back().w, line.width, 0.01f);
}

TEST(LabelSelect, DoubleClickSelectsAWordAndDragExtendsByWords) {
    Fixture fx;
    fx.setup(R"(<Canvas><Label id="t" text="hello world"/></Canvas>)", kSelectable);
    fx.paint();
    engine::ui::Element& label = fx.root().children[0];
    const engine::ui::PaintedTextLine& line = label.painted_text_lines[0];

    fx.click(line.x + 1.0f, line.y + 1.0f, 2);
    EXPECT_EQ(selected(label), "hello");

    engine::ui::pointer_for(fx.world, engine::kPrimaryWindow).down = true;
    const float world_x = line.x + 6.0f * 8.0f + 1.0f;
    engine::ui::update_text_selection(fx.world, world_x, line.y + 1.0f);
    EXPECT_EQ(selected(label), "hello world");
}

TEST(LabelSelect, TripleClickAndUserSelectAllSelectTheWholeLabel) {
    Fixture fx;
    fx.setup(R"(<Canvas><Label id="t" text="hello world"/></Canvas>)", kSelectable);
    fx.paint();
    fx.click(4.0f, 4.0f, 3);
    EXPECT_EQ(selected(fx.root().children[0]), "hello world");

    Fixture all;
    all.setup(R"(<Canvas><Label id="t" text="hello world"/></Canvas>)",
            "Label { user-select: all; width: 200px; font-size: 16px; }");
    all.paint();
    all.click(4.0f, 4.0f, 1);
    EXPECT_EQ(selected(all.root().children[0]), "hello world");
    engine::ui::pointer_for(all.world, engine::kPrimaryWindow).down = true;
    engine::ui::update_text_selection(all.world, 4.0f, 4.0f);
    EXPECT_EQ(selected(all.root().children[0]), "hello world");
}

TEST(LabelSelect, ShiftClickExtendsAndClickOutsideClears) {
    Fixture fx;
    fx.setup(R"(<Canvas><Label id="t" text="hello"/></Canvas>)", kSelectable);
    fx.paint();
    const engine::ui::PaintedTextLine& line = fx.root().children[0].painted_text_lines[0];
    fx.click(line.x + 1.0f, line.y + 1.0f);
    engine::ui::handle_key(fx.world, engine::KeyCode::LShift, true);
    fx.click(line.x + 24.0f, line.y + 1.0f);
    engine::ui::handle_key(fx.world, engine::KeyCode::LShift, false);
    EXPECT_EQ(selected(fx.root().children[0]), "hel");

    fx.click(380.0f, 380.0f);
    EXPECT_EQ(engine::ui::focused_element(fx.world), nullptr);
    EXPECT_FALSE(fx.root().children[0].selection_anchor.has_value());
}

TEST(LabelSelect, RightClickDoesNotChangeTheSelection) {
    Fixture fx;
    fx.setup(R"(<Canvas><Label id="t" text="hello"/></Canvas>)", kSelectable);
    fx.paint();
    fx.click(4.0f, 4.0f, 3);
    ASSERT_EQ(selected(fx.root().children[0]), "hello");
    fx.click(4.0f, 4.0f, 1, false);
    EXPECT_EQ(selected(fx.root().children[0]), "hello");
    EXPECT_TRUE(fx.root().children[0].focused);
}

TEST(LabelSelect, CopyIncludesNewlineAndRespectsAllowCopy) {
    Fixture fx;
    fx.setup(R"(<Canvas><Label id="t" text="hello&#10;world"/></Canvas>)", kSelectable);
    fx.paint();
    fx.click(4.0f, 4.0f, 3);
    press(fx.world, engine::KeyCode::C, true);
    EXPECT_EQ(*fx.clipboard, "hello\nworld");

    press(fx.world, engine::KeyCode::C, true);
    fx.click(380.0f, 380.0f);
    *fx.clipboard = "stale";
    press(fx.world, engine::KeyCode::C, true);
    EXPECT_EQ(*fx.clipboard, "stale");

    Fixture locked;
    locked.setup(R"(<Canvas><Label id="t" text="secret" allow-copy="false"/></Canvas>)", kSelectable);
    locked.paint();
    locked.click(4.0f, 4.0f, 3);
    *locked.clipboard = "";
    press(locked.world, engine::KeyCode::C, true);
    EXPECT_EQ(*locked.clipboard, "");
}

TEST(LabelSelect, EditingKeysDoNotChangeLabelText) {
    Fixture fx;
    fx.setup(R"(<Canvas><Label id="t" text="hello"/></Canvas>)", kSelectable);
    fx.paint();
    fx.click(4.0f, 4.0f, 3);
    engine::ui::handle_text_input(fx.world, "Z");
    press(fx.world, engine::KeyCode::Backspace);
    press(fx.world, engine::KeyCode::Delete);
    press(fx.world, engine::KeyCode::X, true);
    press(fx.world, engine::KeyCode::V, true);
    EXPECT_EQ(fx.root().children[0].text, "hello");
    EXPECT_EQ(*fx.clipboard, "");

    press(fx.world, engine::KeyCode::A, true);
    EXPECT_EQ(selected(fx.root().children[0]), "hello");
    press(fx.world, engine::KeyCode::Left);
    EXPECT_EQ(fx.root().children[0].caret_position, 0u);
}

TEST(LabelSelect, LabelInsideButtonDoesNotStealTheClick) {
    auto vm = std::make_shared<CommandViewModel>();
    Fixture fx;
    fx.setup(R"(<Canvas><Button id="btn" content="Go" command="{binding go}"><Label text="Inside"/></Button></Canvas>)",
            "Label { user-select: text; font-size: 16px; } Button { width: 120px; height: 40px; }", vm);
    fx.paint();
    fx.click(8.0f, 8.0f);
    EXPECT_EQ(vm->runs, 1);
    EXPECT_EQ(engine::ui::focused_element(fx.world), nullptr);
}

TEST(LabelSelect, CommandOnLabelWinsOverUserSelect) {
    auto vm = std::make_shared<CommandViewModel>();
    Fixture fx;
    fx.setup(R"(<Canvas><Label id="t" text="Go" command="{binding go}"/></Canvas>)", kSelectable, vm);
    fx.paint();
    fx.click(4.0f, 4.0f);
    EXPECT_EQ(vm->runs, 1);
    EXPECT_EQ(engine::ui::focused_element(fx.world), nullptr);
    EXPECT_FALSE(fx.root().children[0].selection_anchor.has_value());
}

TEST(LabelSelect, InlineMathTextIsNotSelectableButAllCopiesTheSource) {
    Fixture text;
    text.setup(R"xml(<Canvas><Label id="t" text="a\(x\)"/></Canvas>)xml", kSelectable);
    text.paint();
    text.click(4.0f, 4.0f);
    EXPECT_EQ(engine::ui::focused_element(text.world), nullptr);

    Fixture all;
    all.setup(R"xml(<Canvas><Label id="t" text="a\(x\)"/></Canvas>)xml",
            "Label { user-select: all; width: 200px; font-size: 16px; }");
    all.paint();
    all.click(4.0f, 4.0f);
    EXPECT_EQ(selected(all.root().children[0]), "a\\(x\\)");
    press(all.world, engine::KeyCode::C, true);
    EXPECT_EQ(*all.clipboard, "a\\(x\\)");
}
