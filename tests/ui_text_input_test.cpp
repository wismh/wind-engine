#include <gtest/gtest.h>

#include "ui/painter.h"

#include <engine/ecs/world.h>
#include <engine/ui/builder.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>
#include <vector>

namespace {

class FakePainter final : public engine::ui::IUiPainter {
public:
    struct DrawnLine {
        glm::vec2 from{};
        glm::vec2 to{};
    };

    int lines_drawn = 0;
    int texts_filled = 0;
    int rounded_rects_filled = 0;
    engine::render::Rect last_rounded_rect{};
    std::vector<std::string> filled_texts;
    std::vector<DrawnLine> drawn_lines;

    void save() override {}
    void restore() override {}
    void scissor(const engine::render::Rect&) override {}
    void apply_transform(glm::vec2, float, float) override {}
    void apply_view(glm::vec2, glm::vec2, float) override {}
    void set_opacity(float) override {}
    void fill_rounded_rect(const engine::render::Rect& rect, float, glm::vec4) override {
        ++rounded_rects_filled;
        last_rounded_rect = rect;
    }
    void fill_rounded_rect_gradient(const engine::render::Rect&, float, const engine::ui::Gradient&) override {}
    void stroke_rounded_rect(const engine::render::Rect&, float, float, glm::vec4) override {}
    void draw_line(glm::vec2 from, glm::vec2 to, glm::vec4, float) override {
        ++lines_drawn;
        drawn_lines.push_back(DrawnLine{from, to});
    }
    void stroke_arc(glm::vec2, float, float, float, float, glm::vec4) override {}
    void fill_path(std::span<const engine::ui::PathSegment>, glm::vec4) override {}
    void set_font(engine::AssetId, float) override {}
    void fill_text(std::string_view text, glm::vec2, glm::vec4, engine::ui::UiAlign, engine::ui::UiAlign) override {
        ++texts_filled;
        filled_texts.emplace_back(text);
    }
    void image(engine::AssetId, const engine::render::Rect&) override {}
    void image_repeat(engine::AssetId, const engine::render::Rect&) override {}
    void image_nine_slice(engine::AssetId, const engine::render::Rect&, const engine::ui::BoxInsets&) override {}
    float last_measure_size = 0.0f;

    glm::vec2 measure_text(std::string_view text, engine::AssetId, float size) override {
        last_measure_size = size;
        return {static_cast<float>(text.size()) * size * 0.5f, size};
    }
};

class CardViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<std::string> word{"cat"};
    engine::ui::Bindable<std::string> translation{"кіт"};
    int submits = 0;
    engine::ui::RelayCommand submit;

    CardViewModel() {
        property(engine::ui::intern("word"), word);
        property(engine::ui::intern("translation"), translation);
        command(engine::ui::intern("submit"), submit);
        submit = [this] { ++submits; };
    }
};

engine::ui::Stylesheet test_sheet() {
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(
            "TextInput { width: 100; height: 30; } Button { width: 100; height: 30; }", warnings);
    return sheet.value_or(engine::ui::Stylesheet{});
}

// Installs an in-memory fake clipboard on `world` — headless engine_tests stand-in for the real
// SDL-backed one EngineRuntime wires up (src/render/opengl/clipboard.h/.cpp) via the same
// ui::UiClipboard ctx<> seam.
void install_fake_clipboard(engine::ecs::World& world, std::shared_ptr<std::string> storage) {
    world.ctx<engine::ui::UiClipboard>() = engine::ui::UiClipboard{
            .set_text = [storage](std::string_view text) { *storage = std::string(text); },
            .get_text = [storage]() -> std::optional<std::string> { return *storage; },
    };
}

// Sends Ctrl+`key` as LCtrl-down, `key`-down, LCtrl-up — exercises the modifier tracking
// (ui::UiModifierState) the same way a real Ctrl+C/V/X/A chord arrives from SDL key events.
void press_ctrl(engine::ecs::World& world, engine::KeyCode key) {
    engine::ui::handle_key(world, engine::KeyCode::LCtrl, true);
    engine::ui::handle_key(world, key, true);
    engine::ui::handle_key(world, engine::KeyCode::LCtrl, false);
}

}

TEST(UiTextInput, ParseXmlTag) {
    CardViewModel vm;
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas><TextInput id="word-input" text="{binding word}" command="{binding submit}"/></Canvas>)",
            nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Element* element =
            engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::TextInput);
    ASSERT_NE(element, nullptr);
    EXPECT_EQ(element->id, "word-input");
    EXPECT_EQ(element->text_binding, engine::ui::intern("word"));
    EXPECT_EQ(element->command_binding, engine::ui::intern("submit"));

    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm).has_value());
    EXPECT_EQ(element->text, "cat");
}

TEST(UiTextInput, BuilderNode) {
    using namespace engine::ui;
    auto doc = make_document(
            canvas().add(text_input().with_id("term").text_bind(intern("word")).command_bind(intern("submit"))));
    ASSERT_TRUE(doc.has_value());
    const Element* element = find_by_kind(doc->root, ElementKind::TextInput);
    ASSERT_NE(element, nullptr);
    EXPECT_EQ(element->id, "term");
    EXPECT_EQ(element->text_binding, intern("word"));
    EXPECT_EQ(element->command_binding, intern("submit"));
}

TEST(UiTextInput, FocusAcquiredOnPointerDownAndClearedOnBackgroundOrOtherElement) {
    engine::ecs::World world;
    auto vm = std::make_shared<CardViewModel>();
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas width="200" height="200">
                <Stack>
                    <TextInput id="first" text="{binding word}" width="100" height="30"/>
                    <Button id="btn" content="Submit" width="100" height="30"/>
                </Stack>
            </Canvas>)",
            nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    const engine::render::Rect canvas_rect{0.0f, 0.0f, 200.0f, 200.0f};
    engine::ui::UiCanvas canvas;
    canvas.rect = canvas_rect;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed, test_sheet()});

    engine::ui::begin_frame(world);
    // Click inside TextInput (first child in stack, approx y=15)
    engine::ui::handle_pointer(world, 20.0f, 15.0f);
    engine::ui::Element* focused = engine::ui::focused_element(world);
    ASSERT_NE(focused, nullptr);
    EXPECT_EQ(focused->id, "first");
    EXPECT_TRUE(focused->focused);

    // Click inside Button (approx y=45) clears focus
    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 20.0f, 45.0f);
    EXPECT_EQ(engine::ui::focused_element(world), nullptr);
    EXPECT_FALSE(focused->focused);

    // Click inside TextInput again sets focus
    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 20.0f, 15.0f);
    EXPECT_NE(engine::ui::focused_element(world), nullptr);
    EXPECT_TRUE(focused->focused);

    // Click empty canvas background (x=180, y=180) clears focus
    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 180.0f, 180.0f);
    EXPECT_EQ(engine::ui::focused_element(world), nullptr);
    EXPECT_FALSE(focused->focused);
}

TEST(UiTextInput, EscapeKeyClearsFocus) {
    engine::ecs::World world;
    auto vm = std::make_shared<CardViewModel>();
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas width="200" height="200"><TextInput id="word" text="{binding word}" width="100" height="30"/></Canvas>)",
            nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    const engine::render::Rect canvas_rect{0.0f, 0.0f, 200.0f, 200.0f};
    engine::ui::UiCanvas canvas;
    canvas.rect = canvas_rect;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed, test_sheet()});

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 20.0f, 15.0f);
    engine::ui::Element* focused = engine::ui::focused_element(world);
    ASSERT_NE(focused, nullptr);
    EXPECT_TRUE(focused->focused);

    // Press Escape
    engine::ui::handle_key(world, engine::KeyCode::Escape, true);
    EXPECT_EQ(engine::ui::focused_element(world), nullptr);
    EXPECT_FALSE(focused->focused);
}

TEST(UiTextInput, TextInputAppendsAndWritesToViewModel) {
    engine::ecs::World world;
    auto vm = std::make_shared<CardViewModel>();
    vm->word.set("");
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas width="200" height="200"><TextInput id="word" text="{binding word}" width="100" height="30"/></Canvas>)",
            nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    const engine::render::Rect canvas_rect{0.0f, 0.0f, 200.0f, 200.0f};
    engine::ui::UiCanvas canvas;
    canvas.rect = canvas_rect;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed, test_sheet()});

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 20.0f, 15.0f);
    engine::ui::Element* focused = engine::ui::focused_element(world);
    ASSERT_NE(focused, nullptr);

    engine::ui::handle_text_input(world, "hello");
    EXPECT_EQ(focused->text, "hello");
    EXPECT_EQ(focused->caret_position, 5u);
    EXPECT_EQ(vm->word.get(), "hello");

    engine::ui::handle_text_input(world, " world");
    EXPECT_EQ(focused->text, "hello world");
    EXPECT_EQ(focused->caret_position, 11u);
    EXPECT_EQ(vm->word.get(), "hello world");
}

TEST(UiTextInput, Utf8UkrainianCharactersAndBackspace) {
    engine::ecs::World world;
    auto vm = std::make_shared<CardViewModel>();
    vm->translation.set("");
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas width="200" height="200"><TextInput id="trans" text="{binding translation}" width="100" height="30"/></Canvas>)",
            nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    const engine::render::Rect canvas_rect{0.0f, 0.0f, 200.0f, 200.0f};
    engine::ui::UiCanvas canvas;
    canvas.rect = canvas_rect;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed, test_sheet()});

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 20.0f, 15.0f);
    engine::ui::Element* focused = engine::ui::focused_element(world);
    ASSERT_NE(focused, nullptr);

    // "Слово" is 5 Cyrillic characters, 10 UTF-8 bytes
    engine::ui::handle_text_input(world, "Слово");
    EXPECT_EQ(focused->text, "Слово");
    EXPECT_EQ(focused->caret_position, 10u);
    EXPECT_EQ(vm->translation.get(), "Слово");

    // Backspace deletes "о" (2 bytes), leaving "Слов" (8 bytes)
    engine::ui::handle_key(world, engine::KeyCode::Backspace, true);
    EXPECT_EQ(focused->text, "Слов");
    EXPECT_EQ(focused->caret_position, 8u);
    EXPECT_EQ(vm->translation.get(), "Слов");

    // Backspace deletes "в" (2 bytes), leaving "Сло" (6 bytes)
    engine::ui::handle_key(world, engine::KeyCode::Backspace, true);
    EXPECT_EQ(focused->text, "Сло");
    EXPECT_EQ(focused->caret_position, 6u);
    EXPECT_EQ(vm->translation.get(), "Сло");
}

TEST(UiTextInput, ArrowKeysAndHomeEndNavigation) {
    engine::ecs::World world;
    auto vm = std::make_shared<CardViewModel>();
    vm->word.set("");
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas width="200" height="200"><TextInput id="word" text="{binding word}" width="100" height="30"/></Canvas>)",
            nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    const engine::render::Rect canvas_rect{0.0f, 0.0f, 200.0f, 200.0f};
    engine::ui::UiCanvas canvas;
    canvas.rect = canvas_rect;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed, test_sheet()});

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 20.0f, 15.0f);
    engine::ui::Element* focused = engine::ui::focused_element(world);
    ASSERT_NE(focused, nullptr);

    engine::ui::handle_text_input(world, "ac");
    EXPECT_EQ(focused->caret_position, 2u);

    // Left arrow moves caret between 'a' and 'c'
    engine::ui::handle_key(world, engine::KeyCode::Left, true);
    EXPECT_EQ(focused->caret_position, 1u);

    // Insert 'b' -> "abc"
    engine::ui::handle_text_input(world, "b");
    EXPECT_EQ(focused->text, "abc");
    EXPECT_EQ(focused->caret_position, 2u);
    EXPECT_EQ(vm->word.get(), "abc");

    // Home moves caret to 0
    engine::ui::handle_key(world, engine::KeyCode::Home, true);
    EXPECT_EQ(focused->caret_position, 0u);

    // Delete removes 'a' -> "bc"
    engine::ui::handle_key(world, engine::KeyCode::Delete, true);
    EXPECT_EQ(focused->text, "bc");
    EXPECT_EQ(focused->caret_position, 0u);
    EXPECT_EQ(vm->word.get(), "bc");

    // End moves caret to end
    engine::ui::handle_key(world, engine::KeyCode::End, true);
    EXPECT_EQ(focused->caret_position, 2u);
}

TEST(UiTextInput, UnexecutableSubmitCommandLeavesTheFieldTypeable) {
    engine::ecs::World world;
    auto vm = std::make_shared<CardViewModel>();
    vm->word.set("");
    vm->submit = engine::ui::RelayCommand(
            [vm] { ++vm->submits; }, [vm] { return !vm->word.get().empty(); });

    auto parsed = engine::ui::parse_xml(
            R"(<Canvas width="200" height="200">
                <Stack direction="vertical">
                    <TextInput id="word" text="{binding word}" command="{binding submit}" width="100" height="30"/>
                    <Button id="go" command="{binding submit}" content="Go" width="100" height="30"/>
                </Stack>
            </Canvas>)",
            nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    const engine::render::Rect canvas_rect{0.0f, 0.0f, 200.0f, 200.0f};
    engine::ui::UiCanvas canvas;
    canvas.rect = canvas_rect;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed, test_sheet()});

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 20.0f, 15.0f);
    engine::ui::Element* focused = engine::ui::focused_element(world);
    ASSERT_NE(focused, nullptr);
    EXPECT_EQ(focused->kind, engine::ui::ElementKind::TextInput);
    EXPECT_FALSE(focused->disabled);

    engine::ui::UiInstance& instance = world.get<engine::ui::UiInstance>(entity);
    const engine::ui::Element* button =
            engine::ui::find_by_kind(instance.document.root, engine::ui::ElementKind::Button);
    ASSERT_NE(button, nullptr);
    EXPECT_TRUE(button->disabled);

    engine::ui::handle_key(world, engine::KeyCode::Return, true);
    EXPECT_EQ(vm->submits, 0);

    engine::ui::handle_text_input(world, "a");
    EXPECT_EQ(focused->text, "a");
    EXPECT_EQ(vm->word.get(), "a");
    EXPECT_FALSE(focused->disabled);

    ASSERT_TRUE(engine::ui::apply_bindings(instance.document, *vm).has_value());
    EXPECT_FALSE(focused->disabled);
    EXPECT_FALSE(button->disabled);

    engine::ui::handle_key(world, engine::KeyCode::Return, true);
    EXPECT_EQ(vm->submits, 1);
}

TEST(UiTextInput, ReturnKeyExecutesCommand) {
    engine::ecs::World world;
    auto vm = std::make_shared<CardViewModel>();
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas width="200" height="200"><TextInput id="word" text="{binding word}" command="{binding submit}" width="100" height="30"/></Canvas>)",
            nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    const engine::render::Rect canvas_rect{0.0f, 0.0f, 200.0f, 200.0f};
    engine::ui::UiCanvas canvas;
    canvas.rect = canvas_rect;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed, test_sheet()});

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 20.0f, 15.0f);
    EXPECT_EQ(vm->submits, 0);

    // Press Return. The field stays focused; only a '\n' text event clears it.
    engine::ui::handle_key(world, engine::KeyCode::Return, true);
    EXPECT_EQ(vm->submits, 1);
    engine::ui::Element* focused = engine::ui::focused_element(world);
    ASSERT_NE(focused, nullptr);
    EXPECT_EQ(focused->kind, engine::ui::ElementKind::TextInput);
    EXPECT_TRUE(focused->focused);
}

TEST(UiTextInput, CssFocusPseudoClassMatches) {
    engine::ecs::World world;
    auto vm = std::make_shared<CardViewModel>();
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas width="200" height="200"><TextInput id="word" class="field" text="{binding word}" width="100" height="30"/></Canvas>)",
            nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(".field:focus { color: #ff0000; }", warnings);
    ASSERT_TRUE(sheet.has_value());

    const engine::render::Rect canvas_rect{0.0f, 0.0f, 200.0f, 200.0f};
    engine::ui::UiCanvas canvas;
    canvas.rect = canvas_rect;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed, *sheet});

    engine::ui::UiInstance& instance = world.get<engine::ui::UiInstance>(entity);
    FakePainter painter;

    // Paint while unfocused
    engine::ui::paint_document(instance.document, &*sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = canvas_rect});
    EXPECT_EQ(painter.lines_drawn, 0); // No blinking caret drawn when unfocused

    // Focus TextInput
    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 20.0f, 15.0f);
    EXPECT_TRUE(instance.document.root.children[0].focused);

    // Paint while focused (with delta_time = 0.1s)
    engine::ui::paint_document(instance.document, &*sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = canvas_rect, .delta_time = 0.1f});
    EXPECT_GT(painter.lines_drawn, 0); // Caret drawn when focused
}

TEST(UiTextInput, CssTypeSelectorMatches) {
    engine::ecs::World world;
    auto vm = std::make_shared<CardViewModel>();
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas width="200" height="200"><TextInput id="word" text="{binding word}"/></Canvas>)",
            nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css("TextInput { color: #ff0000; } TextInput:focus { color: #00ff00; }", warnings);
    ASSERT_TRUE(sheet.has_value());

    const engine::render::Rect canvas_rect{0.0f, 0.0f, 200.0f, 200.0f};
    engine::ui::UiCanvas canvas;
    canvas.rect = canvas_rect;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed, *sheet});

    engine::ui::UiInstance& instance = world.get<engine::ui::UiInstance>(entity);
    FakePainter painter;

    // Unfocused
    engine::ui::paint_document(instance.document, &*sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = canvas_rect});
    EXPECT_EQ(painter.lines_drawn, 0);

    // Focused
    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 20.0f, 10.0f);
    EXPECT_TRUE(instance.document.root.children[0].focused);

    engine::ui::paint_document(instance.document, &*sheet, painter,
            engine::ui::UiPaintInput{.canvas_rect = canvas_rect, .delta_time = 0.1f});
    EXPECT_GT(painter.lines_drawn, 0);
}

TEST(UiTextInput, ParseXmlAllowCopyPasteAttributes) {
    CardViewModel vm;
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas>
                <TextInput id="default" text="{binding word}"/>
                <TextInput id="locked" text="{binding translation}" allow-copy="false" allow-paste="false"/>
            </Canvas>)",
            nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());

    const engine::ui::Element& defaulted = parsed->root.children[0];
    EXPECT_TRUE(defaulted.allow_copy);
    EXPECT_TRUE(defaulted.allow_paste);

    const engine::ui::Element& locked = parsed->root.children[1];
    EXPECT_FALSE(locked.allow_copy);
    EXPECT_FALSE(locked.allow_paste);
}

namespace {

// Shared setup for the clipboard tests below: a focused "word" TextInput backed by
// CardViewModel::word, plus an in-memory fake clipboard.
struct ClipboardFixture {
    engine::ecs::World world;
    std::shared_ptr<CardViewModel> vm = std::make_shared<CardViewModel>();
    std::shared_ptr<std::string> clipboard_storage = std::make_shared<std::string>();
    engine::ui::Element* focused = nullptr;

    explicit ClipboardFixture(std::string_view initial_text, bool allow_copy = true, bool allow_paste = true) {
        vm->word.set(std::string(initial_text));
        const std::string xml = std::string(R"(<Canvas width="200" height="200"><TextInput id="word" text="{binding word}" )") +
                "allow-copy=\"" + (allow_copy ? "true" : "false") + "\" " +
                "allow-paste=\"" + (allow_paste ? "true" : "false") + "\" " +
                R"(width="100" height="30"/></Canvas>)";
        auto parsed = engine::ui::parse_xml(xml, nullptr, vm.get());
        EXPECT_TRUE(parsed.has_value());

        engine::ui::UiCanvas canvas;
        canvas.rect = engine::render::Rect{0.0f, 0.0f, 200.0f, 200.0f};
        canvas.fit = engine::ui::UiFit::Fixed;
        canvas.data_context = vm;

        const engine::ecs::Entity entity = world.create();
        world.emplace<engine::ui::UiCanvas>(entity, canvas);
        world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed, test_sheet()});

        install_fake_clipboard(world, clipboard_storage);

        engine::ui::begin_frame(world);
        engine::ui::handle_pointer(world, 20.0f, 15.0f);
        focused = engine::ui::focused_element(world);
    }
};

}

TEST(UiTextInput, CtrlAThenCtrlCCopiesWholeFieldToClipboard) {
    ClipboardFixture fx("hello");
    ASSERT_NE(fx.focused, nullptr);

    press_ctrl(fx.world, engine::KeyCode::A);
    ASSERT_TRUE(fx.focused->selection_anchor.has_value());
    EXPECT_EQ(*fx.focused->selection_anchor, 0u);
    EXPECT_EQ(fx.focused->caret_position, 5u);

    press_ctrl(fx.world, engine::KeyCode::C);
    EXPECT_EQ(*fx.clipboard_storage, "hello");
    EXPECT_EQ(fx.focused->text, "hello"); // copy does not mutate the field
}

TEST(UiTextInput, CtrlCWithoutSelectionIsNoOp) {
    ClipboardFixture fx("hello");
    ASSERT_NE(fx.focused, nullptr);
    *fx.clipboard_storage = "unchanged";

    press_ctrl(fx.world, engine::KeyCode::C);
    EXPECT_EQ(*fx.clipboard_storage, "unchanged");
}

TEST(UiTextInput, CtrlAThenCtrlXCutsWholeField) {
    ClipboardFixture fx("hello");
    ASSERT_NE(fx.focused, nullptr);

    press_ctrl(fx.world, engine::KeyCode::A);
    press_ctrl(fx.world, engine::KeyCode::X);

    EXPECT_EQ(*fx.clipboard_storage, "hello");
    EXPECT_EQ(fx.focused->text, "");
    EXPECT_EQ(fx.focused->caret_position, 0u);
    EXPECT_FALSE(fx.focused->selection_anchor.has_value());
    EXPECT_EQ(fx.vm->word.get(), "");
}

TEST(UiTextInput, CtrlVPastesAtCaretWithoutSelection) {
    ClipboardFixture fx("ac");
    ASSERT_NE(fx.focused, nullptr);
    // No painter registered (headless) -> the focusing click fell back to caret == text.size().
    ASSERT_EQ(fx.focused->caret_position, 2u);
    *fx.clipboard_storage = "b";

    press_ctrl(fx.world, engine::KeyCode::V);

    EXPECT_EQ(fx.focused->text, "acb");
    EXPECT_EQ(fx.focused->caret_position, 3u);
    EXPECT_EQ(fx.vm->word.get(), "acb");
}

TEST(UiTextInput, CtrlAThenCtrlVReplacesWholeField) {
    ClipboardFixture fx("hello");
    ASSERT_NE(fx.focused, nullptr);
    *fx.clipboard_storage = "bye";

    press_ctrl(fx.world, engine::KeyCode::A);
    press_ctrl(fx.world, engine::KeyCode::V);

    EXPECT_EQ(fx.focused->text, "bye");
    EXPECT_EQ(fx.focused->caret_position, 3u);
    EXPECT_FALSE(fx.focused->selection_anchor.has_value());
    EXPECT_EQ(fx.vm->word.get(), "bye");
}

TEST(UiTextInput, CtrlCWithPartialSelectionCopiesOnlyThatRange) {
    ClipboardFixture fx("hello");
    ASSERT_NE(fx.focused, nullptr);
    // Caret starts at text.size() (5, headless click fallback). Shift+Left twice selects "lo".
    engine::ui::handle_key(fx.world, engine::KeyCode::LShift, true);
    engine::ui::handle_key(fx.world, engine::KeyCode::Left, true);
    engine::ui::handle_key(fx.world, engine::KeyCode::Left, true);
    engine::ui::handle_key(fx.world, engine::KeyCode::LShift, false);
    ASSERT_TRUE(fx.focused->selection_anchor.has_value());
    EXPECT_EQ(*fx.focused->selection_anchor, 5u);
    EXPECT_EQ(fx.focused->caret_position, 3u);

    press_ctrl(fx.world, engine::KeyCode::C);
    EXPECT_EQ(*fx.clipboard_storage, "lo");
    EXPECT_EQ(fx.focused->text, "hello"); // copy never mutates
}

TEST(UiTextInput, TypingWithPartialSelectionReplacesExactlyThatRange) {
    ClipboardFixture fx("hello");
    ASSERT_NE(fx.focused, nullptr);
    // Select "lo" (see above), then type over it.
    engine::ui::handle_key(fx.world, engine::KeyCode::LShift, true);
    engine::ui::handle_key(fx.world, engine::KeyCode::Left, true);
    engine::ui::handle_key(fx.world, engine::KeyCode::Left, true);
    engine::ui::handle_key(fx.world, engine::KeyCode::LShift, false);

    engine::ui::handle_text_input(fx.world, "!!");

    EXPECT_EQ(fx.focused->text, "hel!!");
    EXPECT_EQ(fx.focused->caret_position, 5u);
    EXPECT_FALSE(fx.focused->selection_anchor.has_value());
    EXPECT_EQ(fx.vm->word.get(), "hel!!");
}

TEST(UiTextInput, BackspaceWithPartialSelectionErasesExactlyThatRange) {
    ClipboardFixture fx("hello");
    ASSERT_NE(fx.focused, nullptr);
    engine::ui::handle_key(fx.world, engine::KeyCode::LShift, true);
    engine::ui::handle_key(fx.world, engine::KeyCode::Left, true);
    engine::ui::handle_key(fx.world, engine::KeyCode::Left, true);
    engine::ui::handle_key(fx.world, engine::KeyCode::LShift, false);

    engine::ui::handle_key(fx.world, engine::KeyCode::Backspace, true);

    EXPECT_EQ(fx.focused->text, "hel");
    EXPECT_EQ(fx.focused->caret_position, 3u);
    EXPECT_FALSE(fx.focused->selection_anchor.has_value());
}

TEST(UiTextInput, ShiftLeftThenPlainLeftCollapsesToSelectionStart) {
    ClipboardFixture fx("hello");
    ASSERT_NE(fx.focused, nullptr);
    engine::ui::handle_key(fx.world, engine::KeyCode::LShift, true);
    engine::ui::handle_key(fx.world, engine::KeyCode::Left, true);
    engine::ui::handle_key(fx.world, engine::KeyCode::Left, true);
    engine::ui::handle_key(fx.world, engine::KeyCode::LShift, false);
    ASSERT_TRUE(fx.focused->selection_anchor.has_value());

    // Plain (unshifted) Left collapses to the selection's start, not one more char left.
    engine::ui::handle_key(fx.world, engine::KeyCode::Left, true);
    EXPECT_EQ(fx.focused->caret_position, 3u);
    EXPECT_FALSE(fx.focused->selection_anchor.has_value());
}

TEST(UiTextInput, MouseDownThenMoveSelectsARange) {
    ClipboardFixture fx("hello");
    ASSERT_NE(fx.focused, nullptr);

    // No painter (headless) -> both the initial click and the drag-move fall back to
    // caret == text.size(); confirm the drag path still runs (anchor stays put at 5, caret
    // stays clamped at 5) rather than crashing or silently no-op-ing while the button is down.
    engine::ui::pointer_for(fx.world, engine::kPrimaryWindow).down = true;
    engine::ui::update_text_selection(fx.world, 5.0f, 0.0f);
    EXPECT_EQ(fx.focused->caret_position, 5u);
    EXPECT_EQ(*fx.focused->selection_anchor, 5u);
}

TEST(UiTextInput, AllowCopyFalseBlocksCopyAndCut) {
    ClipboardFixture fx("secret", /*allow_copy=*/false, /*allow_paste=*/true);
    ASSERT_NE(fx.focused, nullptr);
    ASSERT_FALSE(fx.focused->allow_copy);

    press_ctrl(fx.world, engine::KeyCode::A);
    press_ctrl(fx.world, engine::KeyCode::C);
    EXPECT_EQ(*fx.clipboard_storage, "");

    press_ctrl(fx.world, engine::KeyCode::X);
    EXPECT_EQ(fx.focused->text, "secret"); // cut needs allow_copy too — field untouched
}

TEST(UiTextInput, AllowPasteFalseBlocksPaste) {
    ClipboardFixture fx("secret", /*allow_copy=*/false, /*allow_paste=*/false);
    ASSERT_NE(fx.focused, nullptr);
    ASSERT_FALSE(fx.focused->allow_paste);
    *fx.clipboard_storage = "hijacked";

    press_ctrl(fx.world, engine::KeyCode::V);
    EXPECT_EQ(fx.focused->text, "secret");
}

TEST(UiTextInput, ReleasingCtrlThenPressingCTypesLiteralLetter) {
    ClipboardFixture fx("");
    ASSERT_NE(fx.focused, nullptr);
    *fx.clipboard_storage = "not-this";

    // Ctrl down, Ctrl up (released before the letter), then a plain 'c' — must type, not copy.
    engine::ui::handle_key(fx.world, engine::KeyCode::LCtrl, true);
    engine::ui::handle_key(fx.world, engine::KeyCode::LCtrl, false);
    engine::ui::handle_text_input(fx.world, "c");

    EXPECT_EQ(fx.focused->text, "c");
    EXPECT_EQ(*fx.clipboard_storage, "not-this");
}

TEST(UiTextInput, FocusLossClearsSelection) {
    ClipboardFixture fx("hello");
    ASSERT_NE(fx.focused, nullptr);

    press_ctrl(fx.world, engine::KeyCode::A);
    EXPECT_TRUE(fx.focused->selection_anchor.has_value());

    engine::ui::handle_key(fx.world, engine::KeyCode::Escape, true);
    EXPECT_FALSE(fx.focused->selection_anchor.has_value());

    // Re-focus without Ctrl+A again: Ctrl+C must be a no-op.
    engine::ui::begin_frame(fx.world);
    engine::ui::handle_pointer(fx.world, 20.0f, 15.0f);
    *fx.clipboard_storage = "";
    press_ctrl(fx.world, engine::KeyCode::C);
    EXPECT_EQ(*fx.clipboard_storage, "");
}

TEST(UiTextInput, ClickPositionsCaretAtNearestGlyphBoundary) {
    engine::ecs::World world;
    auto vm = std::make_shared<CardViewModel>();
    vm->word.set("ac");
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas width="200" height="200"><TextInput id="word" text="{binding word}" width="100" height="30"/></Canvas>)",
            nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    auto sheet = test_sheet();
    const engine::render::Rect canvas_rect{0.0f, 0.0f, 200.0f, 200.0f};
    engine::ui::UiCanvas canvas;
    canvas.rect = canvas_rect;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed, sheet});

    FakePainter painter;
    world.ctx<engine::ui::UiLayoutPainters>().resolve = [&](engine::WindowId) -> engine::ui::IUiPainter* {
        return &painter;
    };

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 20.0f, 15.0f); // no metrics painted yet -> fallback focus click
    engine::ui::Element* focused = engine::ui::focused_element(world);
    ASSERT_NE(focused, nullptr);
    EXPECT_EQ(focused->caret_position, 2u); // fallback: end of "ac"

    engine::ui::UiInstance& instance = world.get<engine::ui::UiInstance>(entity);
    engine::ui::paint_document(
            instance.document, &sheet, painter, engine::ui::UiPaintInput{.canvas_rect = canvas_rect});
    ASSERT_GT(focused->painted_font_size_px, 0.0f);

    // Click exactly at the boundary after 'a' (painted_content_origin_x + width("a")) — the
    // production caret_index_for_click() search must land on index 1, not 0 or 2.
    const float width_a =
            painter.measure_text("a", focused->font_family, focused->painted_font_size_px).x;
    const float click_x = focused->painted_content_origin_x + width_a;

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, click_x, 15.0f);
    EXPECT_EQ(focused->caret_position, 1u);
    EXPECT_EQ(*focused->selection_anchor, 1u); // a plain click clears any selection
}

TEST(UiTextInput, SelectionHighlightOnlyPaintedWhenARealSelectionExists) {
    engine::ecs::World world;
    auto vm = std::make_shared<CardViewModel>();
    vm->word.set("hello");
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas width="200" height="200"><TextInput id="word" text="{binding word}" width="100" height="30"/></Canvas>)",
            nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    auto sheet = test_sheet();
    const engine::render::Rect canvas_rect{0.0f, 0.0f, 200.0f, 200.0f};
    engine::ui::UiCanvas canvas;
    canvas.rect = canvas_rect;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed, sheet});

    FakePainter painter;
    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 20.0f, 15.0f);
    engine::ui::Element* focused = engine::ui::focused_element(world);
    ASSERT_NE(focused, nullptr);

    engine::ui::UiInstance& instance = world.get<engine::ui::UiInstance>(entity);

    // No selection yet: only the caret (a line), no highlight rect.
    engine::ui::paint_document(
            instance.document, &sheet, painter, engine::ui::UiPaintInput{.canvas_rect = canvas_rect});
    EXPECT_EQ(painter.rounded_rects_filled, 0);

    // Ctrl+A selects the whole field: the next paint must fill a highlight rect.
    engine::ui::handle_key(world, engine::KeyCode::LCtrl, true);
    engine::ui::handle_key(world, engine::KeyCode::A, true);
    engine::ui::handle_key(world, engine::KeyCode::LCtrl, false);
    ASSERT_TRUE(focused->selection_anchor.has_value());

    engine::ui::paint_document(
            instance.document, &sheet, painter, engine::ui::UiPaintInput{.canvas_rect = canvas_rect});
    EXPECT_EQ(painter.rounded_rects_filled, 1);
}

// Regression: paint.cpp's highlight rect used to compute sel_start as
// min(*selection_anchor, text.size()) instead of min(*selection_anchor, caret_position) — correct
// (and visible) only when anchor < caret (selecting left-to-right). Selecting right-to-left
// (Shift+Left shrinking the caret below the anchor) left sel_start == sel_end, a zero-width rect
// that never rendered even though the selection itself (copy/paste, etc.) was otherwise correct.
TEST(UiTextInput, SelectionHighlightRendersRightToLeftToo) {
    engine::ecs::World world;
    auto vm = std::make_shared<CardViewModel>();
    vm->word.set("hello");
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas width="200" height="200"><TextInput id="word" text="{binding word}" width="100" height="30"/></Canvas>)",
            nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    auto sheet = test_sheet();
    const engine::render::Rect canvas_rect{0.0f, 0.0f, 200.0f, 200.0f};
    engine::ui::UiCanvas canvas;
    canvas.rect = canvas_rect;
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed, sheet});

    FakePainter painter;
    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 20.0f, 15.0f);
    engine::ui::Element* focused = engine::ui::focused_element(world);
    ASSERT_NE(focused, nullptr);
    ASSERT_EQ(focused->caret_position, 5u); // headless fallback: click lands at end of "hello"

    // Shift+Left twice: anchor arms at 5 (the caret's position before the first move) and stays
    // there while caret_position drops to 3 -> anchor(5) > caret(3), a right-to-left selection.
    engine::ui::handle_key(world, engine::KeyCode::LShift, true);
    engine::ui::handle_key(world, engine::KeyCode::Left, true);
    engine::ui::handle_key(world, engine::KeyCode::Left, true);
    engine::ui::handle_key(world, engine::KeyCode::LShift, false);
    ASSERT_TRUE(focused->selection_anchor.has_value());
    ASSERT_EQ(*focused->selection_anchor, 5u);
    ASSERT_EQ(focused->caret_position, 3u);

    engine::ui::UiInstance& instance = world.get<engine::ui::UiInstance>(entity);
    engine::ui::paint_document(
            instance.document, &sheet, painter, engine::ui::UiPaintInput{.canvas_rect = canvas_rect});

    EXPECT_EQ(painter.rounded_rects_filled, 1);
    const float expected_width = painter.measure_text("lo", focused->font_family, focused->painted_font_size_px).x;
    EXPECT_GT(painter.last_rounded_rect.w, 0.0f);
    EXPECT_FLOAT_EQ(painter.last_rounded_rect.w, expected_width);
}

TEST(UiTextInput, MapTextInputAreaScalesBorderAndCaret) {
    engine::ui::LayoutBoxes boxes;
    boxes.border = {10.0f, 20.0f, 100.0f, 30.0f};
    boxes.content = {14.0f, 24.0f, 92.0f, 22.0f};
    engine::ui::UiCanvasSpace space;
    space.offset = {8.0f, 16.0f};
    space.scale = 2.0f;

    const engine::ui::TextInputScreenArea area =
            engine::ui::map_text_input_area(boxes, 12.0f, engine::ui::UiAlign::Start, space);
    EXPECT_FLOAT_EQ(area.rect.x, 28.0f);
    EXPECT_FLOAT_EQ(area.rect.y, 56.0f);
    EXPECT_FLOAT_EQ(area.rect.w, 200.0f);
    EXPECT_FLOAT_EQ(area.rect.h, 60.0f);
    // Caret layout x is 14 + 12. Window x is 8 + 26 * 2. Cursor is that minus rect.x.
    EXPECT_FLOAT_EQ(area.cursor, 32.0f);

    const engine::ui::TextInputScreenArea centered =
            engine::ui::map_text_input_area(boxes, 12.0f, engine::ui::UiAlign::Center, space);
    EXPECT_FLOAT_EQ(centered.rect.x, area.rect.x);
    EXPECT_FLOAT_EQ(centered.cursor, 124.0f);

    const engine::ui::TextInputScreenArea past_end =
            engine::ui::map_text_input_area(boxes, 10000.0f, engine::ui::UiAlign::Start, space);
    EXPECT_FLOAT_EQ(past_end.cursor, past_end.rect.w);
}

TEST(UiTextInput, ScrolledTextInputMapsToWindowPixels) {
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas>
                <Stack id="scroller" width="200" height="100">
                    <TextInput id="field" width="80" height="24"/>
                </Stack>
            </Canvas>)");
    ASSERT_TRUE(parsed.has_value());

    const engine::render::Rect screen{40.0f, 20.0f, 800.0f, 600.0f};
    const glm::vec2 reference{400.0f, 300.0f};
    engine::ui::UiCanvas canvas;
    canvas.rect = screen;
    canvas.fit = engine::ui::UiFit::ScaleWithScreenSize;
    canvas.reference_size = reference;
    canvas.window = engine::kPrimaryWindow;

    engine::ecs::World world;
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{std::move(*parsed)});

    engine::ui::UiInstance& instance = world.get<engine::ui::UiInstance>(entity);
    ASSERT_FALSE(instance.document.root.children.empty());
    engine::ui::Element& scroller = instance.document.root.children[0];
    ASSERT_EQ(scroller.kind, engine::ui::ElementKind::Stack);
    ASSERT_FALSE(scroller.children.empty());
    engine::ui::Element& field = scroller.children[0];
    ASSERT_EQ(field.kind, engine::ui::ElementKind::TextInput);
    scroller.width = engine::ui::Length{200.0f, engine::ui::LengthUnit::Px};
    scroller.height = engine::ui::Length{100.0f, engine::ui::LengthUnit::Px};
    field.width = engine::ui::Length{80.0f, engine::ui::LengthUnit::Px};
    field.height = engine::ui::Length{24.0f, engine::ui::LengthUnit::Px};

    const engine::ui::UiCanvasSpace space =
            engine::ui::canvas_layout_space(screen, canvas.fit, canvas.reference_size);
    engine::ui::layout(instance.document, space.layout_rect);

    ASSERT_FLOAT_EQ(field.layout_rect.x, 0.0f);
    ASSERT_FLOAT_EQ(field.layout_rect.y, 0.0f);
    ASSERT_FLOAT_EQ(field.layout_rect.w, 80.0f);
    ASSERT_FLOAT_EQ(field.layout_rect.h, 24.0f);

    scroller.scroll_x = 12.0f;
    scroller.scroll_y = 30.0f;
    engine::ui::set_focus(world, engine::kPrimaryWindow, entity, &field);

    const engine::ui::LayoutBoxes boxes = engine::ui::layout_boxes(instance.document.root, field);
    EXPECT_FLOAT_EQ(boxes.border.x, -12.0f);
    EXPECT_FLOAT_EQ(boxes.border.y, -30.0f);
    EXPECT_FLOAT_EQ(boxes.border.w, 80.0f);
    EXPECT_FLOAT_EQ(boxes.border.h, 24.0f);

    const std::optional<engine::ui::TextInputScreenArea> area =
            engine::ui::focused_text_input_area(world, engine::kPrimaryWindow);
    ASSERT_TRUE(area.has_value());
    // scale = 800/400 = 2, offset = (40, 20). Window rect is the scrolled border, not the layout rect.
    EXPECT_FLOAT_EQ(area->rect.x, 16.0f);
    EXPECT_FLOAT_EQ(area->rect.y, -40.0f);
    EXPECT_FLOAT_EQ(area->rect.w, 160.0f);
    EXPECT_FLOAT_EQ(area->rect.h, 48.0f);
    EXPECT_FLOAT_EQ(area->cursor, 0.0f);
    EXPECT_NE(area->rect.w, boxes.border.w);
}

TEST(UiTextInput, TextInputAreaMeasuresPrefixInLayoutPixels) {
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas><TextInput id="field" width="80" height="24" text="ab"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());

    const engine::render::Rect screen{40.0f, 20.0f, 800.0f, 600.0f};
    const glm::vec2 reference{400.0f, 300.0f};
    engine::ui::UiCanvas canvas;
    canvas.rect = screen;
    canvas.fit = engine::ui::UiFit::ScaleWithScreenSize;
    canvas.reference_size = reference;

    engine::ecs::World world;
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{std::move(*parsed)});

    engine::ui::UiInstance& instance = world.get<engine::ui::UiInstance>(entity);
    engine::ui::layout(instance.document, {0.0f, 0.0f, reference.x, reference.y});
    engine::ui::Element* field = engine::ui::find_by_kind(instance.document.root, engine::ui::ElementKind::TextInput);
    ASSERT_NE(field, nullptr);
    field->font_size = {20.0f, engine::ui::LengthUnit::Px};
    field->text = "ab";
    field->caret_position = 2;
    engine::ui::set_focus(world, engine::kPrimaryWindow, entity, field);

    FakePainter painter;
    world.ctx<engine::ui::UiLayoutPainters>().resolve = [&painter](engine::WindowId) { return &painter; };

    const std::optional<engine::ui::TextInputScreenArea> area =
            engine::ui::focused_text_input_area(world, engine::kPrimaryWindow);
    ASSERT_TRUE(area.has_value());
    EXPECT_FLOAT_EQ(painter.last_measure_size, 20.0f);
    // FakePainter width is glyph count * size * 0.5. Canvas scale is 2 and must not be in that size.
    EXPECT_FLOAT_EQ(area->cursor, 40.0f);
}

TEST(UiTextInput, OnlyEnabledTextInputMapsScreenArea) {
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas>
                <Label id="cap" text="Hello" width="80" height="20"/>
                <TextInput id="field" width="80" height="24"/>
            </Canvas>)");
    ASSERT_TRUE(parsed.has_value());

    engine::ui::UiCanvas canvas;
    canvas.rect = {0.0f, 0.0f, 200.0f, 200.0f};
    canvas.fit = engine::ui::UiFit::Fixed;

    engine::ecs::World world;
    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{std::move(*parsed)});
    engine::ui::UiInstance& instance = world.get<engine::ui::UiInstance>(entity);

    engine::ui::Element* label = engine::ui::find_by_kind(instance.document.root, engine::ui::ElementKind::Label);
    engine::ui::Element* field = engine::ui::find_by_kind(instance.document.root, engine::ui::ElementKind::TextInput);
    ASSERT_NE(label, nullptr);
    ASSERT_NE(field, nullptr);

    EXPECT_FALSE(engine::ui::focused_text_input_area(world, engine::kPrimaryWindow).has_value());

    engine::ui::set_focus(world, engine::kPrimaryWindow, entity, label);
    EXPECT_FALSE(engine::ui::focused_text_input_area(world, engine::kPrimaryWindow).has_value());

    field->disabled = true;
    engine::ui::set_focus(world, engine::kPrimaryWindow, entity, field);
    EXPECT_FALSE(engine::ui::focused_text_input_area(world, engine::kPrimaryWindow).has_value());

    field->disabled = false;
    EXPECT_TRUE(engine::ui::focused_text_input_area(world, engine::kPrimaryWindow).has_value());
}

TEST(UiTextInput, NewlineSubmitsAndClearsFocus) {
    engine::ecs::World world;
    auto vm = std::make_shared<CardViewModel>();
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas width="200" height="200"><TextInput id="word" text="{binding word}" command="{binding submit}" width="100" height="30"/></Canvas>)",
            nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    engine::ui::UiCanvas canvas;
    canvas.rect = {0.0f, 0.0f, 200.0f, 200.0f};
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed, test_sheet()});

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 20.0f, 15.0f);
    engine::ui::Element* focused = engine::ui::focused_element(world);
    ASSERT_NE(focused, nullptr);
    EXPECT_EQ(focused->text, "cat");

    engine::ui::handle_text_input(world, "\n");
    EXPECT_EQ(vm->submits, 1);
    EXPECT_EQ(focused->text, "cat");
    EXPECT_EQ(vm->word.get(), "cat");
    EXPECT_EQ(engine::ui::focused_element(world), nullptr);
    EXPECT_FALSE(focused->focused);
}

TEST(UiTextInput, NewlineInsertsPrefixThenSubmits) {
    engine::ecs::World world;
    auto vm = std::make_shared<CardViewModel>();
    std::string text_at_submit;
    vm->submit = [vm, &text_at_submit] {
        ++vm->submits;
        text_at_submit = vm->word.get();
    };
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas width="200" height="200"><TextInput id="word" text="{binding word}" command="{binding submit}" width="100" height="30"/></Canvas>)",
            nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());

    engine::ui::UiCanvas canvas;
    canvas.rect = {0.0f, 0.0f, 200.0f, 200.0f};
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;

    const engine::ecs::Entity entity = world.create();
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed, test_sheet()});

    ASSERT_TRUE(engine::ui::apply_bindings(world.get<engine::ui::UiInstance>(entity).document, *vm).has_value());
    engine::ui::Element* field =
            engine::ui::find_by_kind(world.get<engine::ui::UiInstance>(entity).document.root,
                                     engine::ui::ElementKind::TextInput);
    ASSERT_NE(field, nullptr);
    field->text = "";
    field->caret_position = 0;
    engine::ui::set_focus(world, engine::kPrimaryWindow, entity, field);

    engine::ui::handle_text_input(world, "hi\n");
    EXPECT_EQ(field->text, "hi");
    EXPECT_EQ(vm->word.get(), "hi");
    EXPECT_EQ(text_at_submit, "hi");
    EXPECT_EQ(vm->submits, 1);
    EXPECT_EQ(engine::ui::focused_element(world), nullptr);
    EXPECT_FALSE(field->focused);
}

namespace {

struct FocusedTextInput {
    engine::ecs::World world;
    std::shared_ptr<CardViewModel> vm = std::make_shared<CardViewModel>();
    engine::ecs::Entity entity{};
    engine::ui::Element* field = nullptr;
    const engine::ui::Stylesheet* sheet = nullptr;

    explicit FocusedTextInput(bool command = false) {
        std::string xml = R"(<Canvas width="200" height="200"><TextInput id="word" text="{binding word}")";
        if (command) {
            xml += R"( command="{binding submit}")";
        }
        xml += R"( width="100" height="30"/></Canvas>)";
        auto parsed = engine::ui::parse_xml(xml, nullptr, vm.get());
        EXPECT_TRUE(parsed.has_value());
        if (!parsed.has_value()) {
            return;
        }
        engine::ui::UiCanvas canvas;
        canvas.rect = {0.0f, 0.0f, 200.0f, 200.0f};
        canvas.fit = engine::ui::UiFit::Fixed;
        canvas.data_context = vm;
        entity = world.create();
        world.emplace<engine::ui::UiCanvas>(entity, canvas);
        world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{std::move(*parsed), test_sheet()});
        engine::ui::UiInstance& instance = world.get<engine::ui::UiInstance>(entity);
        EXPECT_TRUE(engine::ui::apply_bindings(instance.document, *vm).has_value());
        field = engine::ui::find_by_kind(instance.document.root, engine::ui::ElementKind::TextInput);
        sheet = instance.stylesheet.has_value() ? &instance.stylesheet.value() : nullptr;
        if (field == nullptr) {
            return;
        }
        engine::ui::set_focus(world, engine::kPrimaryWindow, entity, field);
        field->caret_position = field->text.size();
    }
};

bool painted_horizontal_line(const FakePainter& painter) {
    for (const FakePainter::DrawnLine& line : painter.drawn_lines) {
        if (line.from.y == line.to.y && line.from.x != line.to.x) {
            return true;
        }
    }
    return false;
}

}

TEST(UiTextInput, PreeditStaysOffTheBinding) {
    FocusedTextInput fx;
    ASSERT_NE(fx.field, nullptr);
    EXPECT_EQ(fx.field->text, "cat");
    const std::size_t caret = fx.field->caret_position;
    fx.field->selection_anchor = 1;

    engine::ui::handle_text_editing(fx.world, "при", 1, 2);

    EXPECT_EQ(fx.field->composition, "при");
    EXPECT_EQ(fx.field->composition_start, 1);
    EXPECT_EQ(fx.field->composition_length, 2);
    EXPECT_EQ(fx.field->text, "cat");
    EXPECT_EQ(fx.vm->word.get(), "cat");
    EXPECT_EQ(fx.field->caret_position, caret);
    EXPECT_EQ(fx.field->selection_anchor, 1u);

    engine::ui::UiInstance& instance = fx.world.get<engine::ui::UiInstance>(fx.entity);
    ASSERT_TRUE(engine::ui::apply_bindings(instance.document, *fx.vm).has_value());
    EXPECT_EQ(fx.field->composition, "при");
    EXPECT_EQ(fx.field->composition_start, 1);
    EXPECT_EQ(fx.field->composition_length, 2);
    EXPECT_EQ(fx.field->text, "cat");
    EXPECT_EQ(fx.vm->word.get(), "cat");
}

TEST(UiTextInput, EmptyEditingClearsComposition) {
    FocusedTextInput fx;
    ASSERT_NE(fx.field, nullptr);
    engine::ui::handle_text_editing(fx.world, "при", 1, 1);
    engine::ui::handle_text_editing(fx.world, "", 0, 3);

    EXPECT_TRUE(fx.field->composition.empty());
    EXPECT_EQ(fx.field->composition_start, -1);
    EXPECT_EQ(fx.field->composition_length, -1);
    EXPECT_EQ(fx.field->text, "cat");
    EXPECT_EQ(fx.vm->word.get(), "cat");
}

TEST(UiTextInput, CompositionStartIsCodePoints) {
    FocusedTextInput fx;
    ASSERT_NE(fx.field, nullptr);
    engine::ui::handle_text_editing(fx.world, "при", 2, -1);

    EXPECT_EQ(fx.field->composition, "при");
    EXPECT_EQ(fx.field->composition.size(), 6u);
    EXPECT_EQ(fx.field->composition_start, 2);
    // Code point 2 is byte 4. A byte index of 2 would land inside U+0440.
    EXPECT_EQ(engine::ui::utf8_byte_offset(fx.field->composition, 2), 4u);
}

TEST(UiTextInput, TextInputCommitsAndClearsPreedit) {
    FocusedTextInput fx;
    ASSERT_NE(fx.field, nullptr);
    engine::ui::handle_text_editing(fx.world, "при", 2, -1);
    engine::ui::handle_text_input(fx.world, "й");

    EXPECT_TRUE(fx.field->composition.empty());
    EXPECT_EQ(fx.field->composition_start, -1);
    EXPECT_EQ(fx.field->composition_length, -1);
    EXPECT_EQ(fx.field->text, "catй");
    EXPECT_EQ(fx.vm->word.get(), "catй");
}

TEST(UiTextInput, KeysWhileComposingLeaveTextAndCaret) {
    FocusedTextInput fx;
    ASSERT_NE(fx.field, nullptr);
    engine::ui::handle_text_editing(fx.world, "при", 1, -1);
    const std::size_t caret = fx.field->caret_position;

    engine::ui::handle_key(fx.world, engine::KeyCode::Backspace, true);
    engine::ui::handle_key(fx.world, engine::KeyCode::Left, true);
    EXPECT_EQ(fx.field->text, "cat");
    EXPECT_EQ(fx.field->caret_position, caret);
    EXPECT_EQ(fx.field->composition, "при");
    EXPECT_EQ(fx.vm->word.get(), "cat");

    engine::ui::handle_key(fx.world, engine::KeyCode::Escape, true);
    EXPECT_EQ(engine::ui::focused_element(fx.world), nullptr);
    EXPECT_FALSE(fx.field->focused);
    EXPECT_TRUE(fx.field->composition.empty());
    EXPECT_EQ(fx.field->composition_start, -1);
    EXPECT_EQ(fx.field->composition_length, -1);
    EXPECT_EQ(fx.field->text, "cat");
    EXPECT_EQ(fx.vm->word.get(), "cat");
}

TEST(UiTextInput, ReturnWhileComposingRunsCommandAndDropsPreedit) {
    FocusedTextInput fx(true);
    ASSERT_NE(fx.field, nullptr);
    std::string seen;
    fx.vm->submit = [vm = fx.vm, &seen] {
        ++vm->submits;
        seen = vm->word.get();
    };
    engine::ui::handle_text_editing(fx.world, "при", 0, -1);

    engine::ui::handle_key(fx.world, engine::KeyCode::Return, true);

    EXPECT_EQ(fx.vm->submits, 1);
    EXPECT_EQ(seen, "cat");
    EXPECT_EQ(engine::ui::focused_element(fx.world), fx.field);
    EXPECT_TRUE(fx.field->focused);
    EXPECT_TRUE(fx.field->composition.empty());
    EXPECT_EQ(fx.field->composition_start, -1);
    EXPECT_EQ(fx.field->composition_length, -1);
    EXPECT_EQ(fx.field->text, "cat");
    EXPECT_EQ(fx.vm->word.get(), "cat");
}

TEST(UiTextInput, CompositionPaintsTextAndUnderline) {
    FocusedTextInput fx;
    ASSERT_NE(fx.field, nullptr);
    engine::ui::handle_text_editing(fx.world, "й", -1, -1);

    FakePainter painter;
    engine::ui::UiInstance& instance = fx.world.get<engine::ui::UiInstance>(fx.entity);
    engine::ui::paint_document(instance.document, fx.sheet, painter,
                               engine::ui::UiPaintInput{.canvas_rect = {0.0f, 0.0f, 200.0f, 200.0f},
                                                        .delta_time = 0.1f});

    bool saw_preedit = false;
    for (const std::string& text : painter.filled_texts) {
        if (text == "й") {
            saw_preedit = true;
        }
    }
    EXPECT_TRUE(saw_preedit);
    EXPECT_TRUE(painted_horizontal_line(painter));
}
