#include <gtest/gtest.h>

#include <engine/ecs/schedule.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/world.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/fatal_error.h>
#include <engine/ui/builder.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

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

class HudViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<std::string> title;
    engine::ui::Bindable<std::string> restart_label;
    engine::ui::RelayCommand restart;

    HudViewModel() {
        property(engine::ui::intern("title"), title);
        property(engine::ui::intern("restart_label"), restart_label);
        command(engine::ui::intern("restart"), restart);
    }
};

constexpr std::string_view kValidXml = R"(
<Canvas>
  <Stack class="hud" direction="vertical">
    <Label class="title" text="{binding title}"/>
    <Button command="{binding restart}" content="{binding restart_label}"/>
  </Stack>
</Canvas>
)";

engine::ui::Node hud_tree() {
    auto hud = engine::ui::stack().with_class("hud").direction(engine::ui::StackDirection::Vertical);
    hud.add(engine::ui::label().with_class("title").text_bind(engine::ui::intern("title")));
    hud.add(engine::ui::button().command_bind(engine::ui::intern("restart")).content_bind(engine::ui::intern("restart_label")));
    return engine::ui::canvas().add(std::move(hud));
}

}

TEST(UiBuilder, MatchesParseXmlHudTree) {
    HudViewModel vm;
    const auto parsed = engine::ui::parse_xml(kValidXml, nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());

    const auto built = engine::ui::make_document(hud_tree());
    ASSERT_TRUE(built.has_value());

    const engine::ui::Element& xml_root = parsed->root;
    const engine::ui::Element& cpp_root = built->root;
    EXPECT_EQ(cpp_root.kind, engine::ui::ElementKind::Canvas);
    ASSERT_EQ(cpp_root.children.size(), 1u);
    EXPECT_EQ(cpp_root.children[0].kind, xml_root.children[0].kind);
    EXPECT_EQ(cpp_root.children[0].classes, xml_root.children[0].classes);
    EXPECT_EQ(cpp_root.children[0].direction, xml_root.children[0].direction);
    ASSERT_EQ(cpp_root.children[0].children.size(), 2u);
    EXPECT_EQ(cpp_root.children[0].children[0].kind, xml_root.children[0].children[0].kind);
    EXPECT_EQ(cpp_root.children[0].children[0].text_binding, xml_root.children[0].children[0].text_binding);
    EXPECT_EQ(cpp_root.children[0].children[1].kind, xml_root.children[0].children[1].kind);
    EXPECT_EQ(cpp_root.children[0].children[1].command_binding, xml_root.children[0].children[1].command_binding);
    EXPECT_EQ(cpp_root.children[0].children[1].content_binding, xml_root.children[0].children[1].content_binding);
}

TEST(UiBuilder, ApplyBindingsUpdatesLabelText) {
    HudViewModel vm;
    vm.title.set("Hello");

    auto document = engine::ui::make_document(
            engine::ui::canvas().add(engine::ui::label().text_bind(engine::ui::intern("title"))));
    ASSERT_TRUE(document.has_value());
    ASSERT_TRUE(engine::ui::apply_bindings(*document, vm).has_value());
    const engine::ui::Element* label = engine::ui::find_by_kind(document->root, engine::ui::ElementKind::Label);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text, "Hello");

    vm.title.set("World");
    ASSERT_TRUE(engine::ui::apply_bindings(*document, vm).has_value());
    EXPECT_EQ(engine::ui::find_by_kind(document->root, engine::ui::ElementKind::Label)->text, "World");
}

TEST(UiBuilder, CommandBindIsHitTargetAfterLayout) {
    auto document = engine::ui::make_document(
            engine::ui::canvas().add(engine::ui::button().command_bind(engine::ui::intern("restart")).content("Go")));
    ASSERT_TRUE(document.has_value());
    engine::ui::layout(*document, engine::render::Rect{0.0f, 0.0f, 100.0f, 100.0f});

    const engine::ui::Element* button = engine::ui::find_by_kind(document->root, engine::ui::ElementKind::Button);
    ASSERT_NE(button, nullptr);
    EXPECT_TRUE(engine::ui::is_bound(button->command_binding));
    EXPECT_EQ(button->command_binding, engine::ui::intern("restart"));

    engine::ui::Element* hit = engine::ui::hit_test(document->root, 8.0f, 8.0f);
    ASSERT_NE(hit, nullptr);
    EXPECT_EQ(hit->kind, engine::ui::ElementKind::Button);
}

TEST(UiBuilder, AddPreservesChildOrder) {
    auto row = engine::ui::stack();
    row.add(engine::ui::label().text("a"));
    row.add(engine::ui::label().text("b"));
    const auto document = engine::ui::make_document(engine::ui::canvas().add(std::move(row)));
    ASSERT_TRUE(document.has_value());
    ASSERT_EQ(document->root.children.size(), 1u);
    ASSERT_EQ(document->root.children[0].children.size(), 2u);
    EXPECT_EQ(document->root.children[0].children[0].text, "a");
    EXPECT_EQ(document->root.children[0].children[1].text, "b");
}

TEST(UiBuilder, MakeDocumentRejectsNonCanvasRoot) {
    RecordingFatalError fatal;
    const auto document = engine::ui::make_document(engine::ui::label().text("nope"), &fatal);
    EXPECT_FALSE(document.has_value());
    EXPECT_EQ(document.error(), engine::ui::UiError::InvalidMarkup);
    EXPECT_GE(fatal.call_count, 1);
}

TEST(UiBuilder, StackDirectionGapAndLine) {
    auto hud = engine::ui::stack().direction(engine::ui::StackDirection::Horizontal).gap(8.0f);
    hud.add(engine::ui::line().with_class("edge"));
    const auto document = engine::ui::make_document(engine::ui::canvas().add(std::move(hud)));
    ASSERT_TRUE(document.has_value());
    const engine::ui::Element& stack = document->root.children[0];
    EXPECT_EQ(stack.direction, engine::ui::StackDirection::Horizontal);
    EXPECT_FLOAT_EQ(stack.gap.value, 8.0f);
    EXPECT_EQ(stack.gap.unit, engine::ui::LengthUnit::Px);
    ASSERT_EQ(stack.children.size(), 1u);
    EXPECT_EQ(stack.children[0].kind, engine::ui::ElementKind::Line);
    EXPECT_EQ(stack.children[0].classes, std::vector<std::string>{"edge"});
}

TEST(UiBuilder, ClassTokensSplitLikeXml) {
    const auto document = engine::ui::make_document(engine::ui::canvas().add(engine::ui::stack().with_class("hud  title")));
    ASSERT_TRUE(document.has_value());
    EXPECT_EQ(document->root.children[0].classes, (std::vector<std::string>{"hud", "title"}));
}

TEST(UiBuilder, CanvasStylesheetAndImageSource) {
    constexpr engine::AssetId kCss{"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb01"};
    constexpr engine::AssetId kTex{"cccccccccccccccccccccccccccccc02"};
    const auto document = engine::ui::make_document(
            engine::ui::canvas().stylesheet(kCss).add(engine::ui::image().source(kTex)));
    ASSERT_TRUE(document.has_value());
    ASSERT_TRUE(document->stylesheet.has_value());
    EXPECT_EQ(*document->stylesheet, kCss);
    const engine::ui::Element* image = engine::ui::find_by_kind(document->root, engine::ui::ElementKind::Image);
    ASSERT_NE(image, nullptr);
    ASSERT_TRUE(image->source.has_value());
    EXPECT_EQ(*image->source, kTex);
}

TEST(UiBuilder, VarAndDragAndItemsControl) {
    auto tmpl = engine::ui::item_template();
    tmpl.add(engine::ui::label().text_bind(engine::ui::intern("mark")));
    auto list = engine::ui::items_control().items_source_bind(engine::ui::intern("cells"));
    list.add(std::move(tmpl));

    auto track = engine::ui::stack()
                         .drag_bind(engine::ui::intern("fraction"))
                         .drag_orientation(engine::ui::StackDirection::Vertical)
                         .var("w", engine::ui::intern("fraction"));

    auto root = engine::ui::canvas();
    root.add(std::move(list));
    root.add(std::move(track));
    const auto document = engine::ui::make_document(std::move(root));
    ASSERT_TRUE(document.has_value());

    const engine::ui::Element* items =
            engine::ui::find_by_kind(document->root, engine::ui::ElementKind::ItemsControl);
    ASSERT_NE(items, nullptr);
    EXPECT_EQ(items->items_source_binding, engine::ui::intern("cells"));
    ASSERT_EQ(items->children.size(), 1u);
    EXPECT_EQ(items->children[0].kind, engine::ui::ElementKind::ItemTemplate);

    ASSERT_EQ(document->root.children.size(), 2u);
    const engine::ui::Element& drag = document->root.children[1];
    EXPECT_EQ(drag.drag_binding, engine::ui::intern("fraction"));
    EXPECT_EQ(drag.drag_orientation, engine::ui::StackDirection::Vertical);
    ASSERT_EQ(drag.custom_property_bindings.size(), 1u);
    EXPECT_EQ(drag.custom_property_bindings[0].name, "w");
    EXPECT_EQ(drag.custom_property_bindings[0].binding, engine::ui::intern("fraction"));
}

TEST(UiBuilder, SpawnCanvasClearsCatalogIdAndKeepsDocument) {
    auto vm = std::make_shared<HudViewModel>();
    vm->title.set("Hello");
    auto document = engine::ui::make_document(
            engine::ui::canvas().add(engine::ui::label().text_bind(engine::ui::intern("title"))));
    ASSERT_TRUE(document.has_value());

    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css("Label { color: #ff0000; }\n", warnings);
    ASSERT_TRUE(sheet.has_value());

    engine::ecs::World world;
    engine::ui::UiCanvas canvas;
    canvas.document = engine::AssetId{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1"};
    canvas.data_context = vm;
    canvas.fit = engine::ui::UiFit::Fixed;

    const engine::ecs::Entity entity =
            engine::ui::spawn_canvas(world, canvas, std::move(*document), std::move(*sheet));

    const auto& spawned = world.get<engine::ui::UiCanvas>(entity);
    EXPECT_FALSE(spawned.document.has_value());
    const engine::ui::UiInstance& instance = world.get<engine::ui::UiInstance>(entity);
    ASSERT_EQ(instance.document.root.children.size(), 1u);
    EXPECT_EQ(instance.document.root.children[0].kind, engine::ui::ElementKind::Label);
    ASSERT_TRUE(instance.stylesheet.has_value());
    ASSERT_EQ(instance.stylesheet->rules.size(), 1u);

    engine::register_engine_systems(world);
    world.run(engine::ecs::Schedule::Frame);
    EXPECT_EQ(engine::ui::find_by_kind(world.get<engine::ui::UiInstance>(entity).document.root,
                      engine::ui::ElementKind::Label)
                      ->text,
            "Hello");
}
