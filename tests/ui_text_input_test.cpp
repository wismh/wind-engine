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
