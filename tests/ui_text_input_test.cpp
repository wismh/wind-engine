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
    int lines_drawn = 0;
    int texts_filled = 0;

    void save() override {}
    void restore() override {}
    void scissor(const engine::render::Rect&) override {}
    void apply_transform(glm::vec2, float, float) override {}
    void apply_view(glm::vec2, glm::vec2, float) override {}
    void set_opacity(float) override {}
    void fill_rounded_rect(const engine::render::Rect&, float, glm::vec4) override {}
    void stroke_rounded_rect(const engine::render::Rect&, float, float, glm::vec4) override {}
    void draw_line(glm::vec2, glm::vec2, glm::vec4, float) override { ++lines_drawn; }
    void set_font(engine::AssetId, float) override {}
    void fill_text(std::string_view, glm::vec2, glm::vec4, engine::ui::UiAlign, engine::ui::UiAlign) override {
        ++texts_filled;
    }
    void image(engine::AssetId, const engine::render::Rect&) override {}
    void image_repeat(engine::AssetId, const engine::render::Rect&) override {}
    void image_nine_slice(engine::AssetId, const engine::render::Rect&, const engine::ui::BoxInsets&) override {}
    glm::vec2 measure_text(std::string_view text, engine::AssetId, float size) override {
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

    // Press Return
    engine::ui::handle_key(world, engine::KeyCode::Return, true);
    EXPECT_EQ(vm->submits, 1);
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
    EXPECT_TRUE(fx.focused->selected_all);

    press_ctrl(fx.world, engine::KeyCode::C);
    EXPECT_EQ(*fx.clipboard_storage, "hello");
    EXPECT_EQ(fx.focused->text, "hello"); // copy does not mutate the field
}

TEST(UiTextInput, CtrlCWithoutSelectAllIsNoOp) {
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
    EXPECT_FALSE(fx.focused->selected_all);
    EXPECT_EQ(fx.vm->word.get(), "");
}

TEST(UiTextInput, CtrlVPastesAtCaretWithoutSelection) {
    ClipboardFixture fx("ac");
    ASSERT_NE(fx.focused, nullptr);
    *fx.clipboard_storage = "b";

    // Caret starts at 0 (bound text, never typed) — Right once lands it between 'a' and 'c'.
    engine::ui::handle_key(fx.world, engine::KeyCode::Right, true);
    press_ctrl(fx.world, engine::KeyCode::V);

    EXPECT_EQ(fx.focused->text, "abc");
    EXPECT_EQ(fx.focused->caret_position, 2u);
    EXPECT_EQ(fx.vm->word.get(), "abc");
}

TEST(UiTextInput, CtrlAThenCtrlVReplacesWholeField) {
    ClipboardFixture fx("hello");
    ASSERT_NE(fx.focused, nullptr);
    *fx.clipboard_storage = "bye";

    press_ctrl(fx.world, engine::KeyCode::A);
    press_ctrl(fx.world, engine::KeyCode::V);

    EXPECT_EQ(fx.focused->text, "bye");
    EXPECT_EQ(fx.focused->caret_position, 3u);
    EXPECT_FALSE(fx.focused->selected_all);
    EXPECT_EQ(fx.vm->word.get(), "bye");
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

TEST(UiTextInput, FocusLossClearsSelectAll) {
    ClipboardFixture fx("hello");
    ASSERT_NE(fx.focused, nullptr);

    press_ctrl(fx.world, engine::KeyCode::A);
    EXPECT_TRUE(fx.focused->selected_all);

    engine::ui::handle_key(fx.world, engine::KeyCode::Escape, true);
    EXPECT_FALSE(fx.focused->selected_all);

    // Re-focus without Ctrl+A again: Ctrl+C must be a no-op.
    engine::ui::begin_frame(fx.world);
    engine::ui::handle_pointer(fx.world, 20.0f, 15.0f);
    *fx.clipboard_storage = "";
    press_ctrl(fx.world, engine::KeyCode::C);
    EXPECT_EQ(*fx.clipboard_storage, "");
}
