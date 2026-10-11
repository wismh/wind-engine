#include <gtest/gtest.h>

#include <engine/ecs/world.h>
#include <engine/ui/bindable.h>
#include <engine/ui/binding_id.h>
#include <engine/ui/builder.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/presentation.h>
#include <engine/ui/stylesheet.h>
#include <engine/ui/view_model.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "fixtures/bind_error_fakes.h"
#include "ui/bind_scan.h"
#include "ui/painter.h"

namespace {

using engine::ui::Element;

Element* find_class(Element& root, std::string_view class_name) {
    if (std::ranges::find(root.classes, class_name) != root.classes.end()) {
        return &root;
    }
    for (Element& child : root.children) {
        if (Element* found = find_class(child, class_name)) {
            return found;
        }
    }
    return nullptr;
}

class RowViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<bool> shown{true};

    RowViewModel() { property(engine::ui::intern("shown"), shown); }
};

class ListViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::BindableList<std::shared_ptr<RowViewModel>> rows;

    ListViewModel() { property(engine::ui::intern("rows"), rows); }
};

class PanelViewModel final : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<bool> shown{true};
    engine::ui::Bindable<float> x;
    engine::ui::Bindable<float> fraction;
    engine::ui::Bindable<std::string> label;

    PanelViewModel() {
        property(engine::ui::intern("shown"), shown);
        property(engine::ui::intern("x"), x);
        property(engine::ui::intern("fraction"), fraction);
        property(engine::ui::intern("label"), label);
    }
};

// One frame of what run_bind and the layout pass do for a document, without a world.
void bind_and_layout(engine::ui::UiDocument& document, engine::ui::ViewModel& vm, const engine::ui::Stylesheet& sheet) {
    ASSERT_TRUE(engine::ui::apply_bindings(document, vm).has_value());
    engine::ui::apply_layout_style(document.root, &sheet, 400.0f, 400.0f);
    engine::ui::layout(document, engine::render::Rect{0.0f, 0.0f, 400.0f, 400.0f});
}

engine::ui::Stylesheet parse_sheet(std::string_view css) {
    std::vector<std::string> warnings;
    auto sheet = engine::ui::parse_css(css, warnings);
    EXPECT_TRUE(sheet.has_value());
    return std::move(*sheet);
}

constexpr std::string_view kColumnXml = R"(<Canvas><Stack class="col"><Stack class="a"/>
<Stack class="b" if="{binding shown}"/><Stack class="c"/></Stack></Canvas>)";
constexpr std::string_view kColumnCss =
        ".col { gap: 10; } .a { width: 50; height: 20; } .b { width: 50; height: 30; } .c { width: 50; height: 40; }";

}

TEST(BindableChange, CppWriteIsReportedOncePerChange) {
    engine::ui::Bindable<int> value{1};
    std::vector<int> seen;
    value.on_changed([&seen](const int& v) { seen.push_back(v); });

    value.set(2);
    value = 3;
    EXPECT_EQ(seen, (std::vector<int>{2, 3}));
    EXPECT_EQ(value.get(), 3);
}

TEST(BindableChange, SameValueIsSilent) {
    engine::ui::Bindable<std::string> text{"a"};
    int calls = 0;
    text.on_changed([&calls](const std::string&) { ++calls; });

    text.set("a");
    text = std::string("a");
    EXPECT_EQ(calls, 0);
    text.set("b");
    EXPECT_EQ(calls, 1);
}

TEST(BindableChange, HandlerIsReplacedAndCanBeRemoved) {
    engine::ui::Bindable<float> value;
    int first = 0;
    int second = 0;
    value.on_changed([&first](const float&) { ++first; });
    value.on_changed([&second](const float&) { ++second; });
    value.set(1.0f);
    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 1);

    value.on_changed({});
    value.set(2.0f);
    EXPECT_EQ(second, 1);
    EXPECT_FLOAT_EQ(value.get(), 2.0f);
}

TEST(BindableChange, WriteThroughTheViewModelIsReported) {
    PanelViewModel vm;
    int calls = 0;
    float last = -1.0f;
    vm.fraction.on_changed([&](const float& v) {
        ++calls;
        last = v;
    });

    EXPECT_TRUE(vm.write_property_float(engine::ui::intern("fraction"), 0.5f));
    EXPECT_EQ(calls, 1);
    EXPECT_FLOAT_EQ(last, 0.5f);
    EXPECT_TRUE(vm.write_property_float(engine::ui::intern("fraction"), 0.5f));
    EXPECT_EQ(calls, 1);
}

TEST(BindableChange, DragFromTheDocumentIsReportedOnceAndNotWhenRepeated) {
    engine::ecs::World world;
    auto vm = std::make_shared<PanelViewModel>();
    int calls = 0;
    vm->fraction.on_changed([&calls](const float&) { ++calls; });

    auto parsed = engine::ui::parse_xml(R"(<Canvas><Image drag="{binding fraction}"/></Canvas>)", nullptr, vm.get());
    ASSERT_TRUE(parsed.has_value());
    const engine::ecs::Entity entity = world.create();
    engine::ui::UiCanvas canvas;
    canvas.rect = {0.0f, 0.0f, 100.0f, 100.0f};
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.data_context = vm;
    world.emplace<engine::ui::UiCanvas>(entity, canvas);
    world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{*parsed});

    engine::ui::begin_frame(world);
    engine::ui::handle_pointer(world, 8.0f, 8.0f);
    EXPECT_EQ(calls, 1);
    EXPECT_FLOAT_EQ(vm->fraction.get(), 0.25f);

    // The same position again writes the same fraction: nothing changed, nothing reported.
    engine::ui::update_drag(world, 8.0f, 8.0f);
    EXPECT_EQ(calls, 1);

    engine::ui::update_drag(world, 16.0f, 8.0f);
    EXPECT_EQ(calls, 2);
    EXPECT_FLOAT_EQ(vm->fraction.get(), 0.5f);
}

TEST(UiIfBinding, FalseRemovesTheElementFromLayout) {
    auto parsed = engine::ui::parse_xml(kColumnXml);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = parse_sheet(kColumnCss);
    PanelViewModel vm;

    vm.shown.set(true);
    bind_and_layout(*parsed, vm, sheet);
    EXPECT_FALSE(find_class(parsed->root, "b")->display_none);
    // a (20) + gap + b (30) + gap.
    EXPECT_FLOAT_EQ(find_class(parsed->root, "c")->layout_rect.y, 70.0f);

    vm.shown.set(false);
    bind_and_layout(*parsed, vm, sheet);
    EXPECT_TRUE(find_class(parsed->root, "b")->display_none);
    EXPECT_FLOAT_EQ(find_class(parsed->root, "b")->layout_rect.h, 0.0f);
    // a (20) + one gap: b leaves neither its height nor a second gap.
    EXPECT_FLOAT_EQ(find_class(parsed->root, "c")->layout_rect.y, 30.0f);
    EXPECT_FLOAT_EQ(find_class(parsed->root, "col")->layout_rect.h, 70.0f);

    vm.shown.set(true);
    bind_and_layout(*parsed, vm, sheet);
    EXPECT_FALSE(find_class(parsed->root, "b")->display_none);
    EXPECT_FLOAT_EQ(find_class(parsed->root, "c")->layout_rect.y, 70.0f);
}

TEST(UiIfBinding, ASheetThatShowsTheElementDoesNotOverrideIt) {
    auto parsed = engine::ui::parse_xml(kColumnXml);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = parse_sheet(std::string(kColumnCss) + " .b { display: block; }");
    PanelViewModel vm;
    vm.shown.set(false);

    bind_and_layout(*parsed, vm, sheet);
    EXPECT_TRUE(find_class(parsed->root, "b")->display_none);
}

TEST(UiIfBinding, TheLayoutGateNoticesAToggle) {
    auto parsed = engine::ui::parse_xml(kColumnXml);
    ASSERT_TRUE(parsed.has_value());
    PanelViewModel vm;
    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm).has_value());
    EXPECT_TRUE(engine::ui::layout_state_changed(parsed->root));
    EXPECT_FALSE(engine::ui::layout_state_changed(parsed->root));

    vm.shown.set(false);
    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm).has_value());
    EXPECT_TRUE(engine::ui::layout_state_changed(parsed->root));
    EXPECT_FALSE(engine::ui::layout_state_changed(parsed->root));
}

TEST(UiIfBinding, BuilderDocumentUsesIfBind) {
    auto document = engine::ui::make_document(engine::ui::canvas().add(
            engine::ui::stack().with_class("b").if_bind(engine::ui::intern("shown"))));
    ASSERT_TRUE(document.has_value());
    PanelViewModel vm;
    vm.shown.set(false);
    ASSERT_TRUE(engine::ui::apply_bindings(*document, vm).has_value());
    EXPECT_TRUE(document->root.children[0].hidden_by_if);
}

TEST(UiIfBinding, MissingNameIsANamedError) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Stack if="{binding nope}"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    PanelViewModel vm;
    fakes::RecordingBindErrors errors;

    const auto applied = engine::ui::apply_bindings(*parsed, vm, &errors);

    ASSERT_FALSE(applied.has_value());
    EXPECT_EQ(applied.error(), engine::ui::UiError::MissingBinding);
    ASSERT_EQ(errors.messages.size(), 1u);
    EXPECT_EQ(errors.messages[0], "UI if binding \"nope\" is not registered on the data context");
    EXPECT_FALSE(parsed->root.children[0].hidden_by_if);
}

TEST(UiIfBinding, ABindableThatIsNotANumberIsANamedError) {
    auto parsed = engine::ui::parse_xml(R"(<Canvas><Stack if="{binding label}"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    PanelViewModel vm;
    fakes::RecordingBindErrors errors;

    ASSERT_FALSE(engine::ui::apply_bindings(*parsed, vm, &errors).has_value());
    ASSERT_EQ(errors.messages.size(), 1u);
    EXPECT_EQ(errors.messages[0], "UI if binding \"label\" must be a Bindable<bool> (or another number)");
}

TEST(UiIfBinding, ResolvesPerRowInsideAnItemTemplateWithoutAnError) {
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas><ItemsControl items_source="{binding rows}"><ItemTemplate><Stack class="row" if="{binding shown}"/>
</ItemTemplate></ItemsControl></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    ListViewModel vm;
    vm.rows.get().push_back(std::make_shared<RowViewModel>());
    vm.rows.get().push_back(std::make_shared<RowViewModel>());
    vm.rows.get()[1]->shown.set(false);
    fakes::RecordingBindErrors errors;

    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm, &errors).has_value());

    EXPECT_TRUE(errors.messages.empty());
    const Element& list = parsed->root.children[0];
    ASSERT_EQ(list.generated_items.size(), 2u);
    EXPECT_FALSE(list.generated_items[0].hidden_by_if);
    EXPECT_TRUE(list.generated_items[1].hidden_by_if);

    vm.rows.get()[0]->shown.set(false);
    vm.rows.get()[1]->shown.set(true);
    ASSERT_TRUE(engine::ui::apply_bindings(*parsed, vm, &errors).has_value());
    EXPECT_TRUE(parsed->root.children[0].generated_items[0].hidden_by_if);
    EXPECT_FALSE(parsed->root.children[0].generated_items[1].hidden_by_if);
}

TEST(UiIfBinding, LiteralIsRejectedAtParse) {
    const auto parsed = engine::ui::parse_xml(R"(<Canvas><Stack if="false"/></Canvas>)");
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), engine::ui::UiError::MissingBinding);
}

TEST(UiIfBinding, ScanFindsTheBindingAsAProperty) {
    const auto binder = engine::ui::scan_bind_tree(R"(<Canvas><Stack if="{binding bodyShown}"/></Canvas>)");
    ASSERT_TRUE(binder.has_value());
    ASSERT_EQ(binder->members.size(), 1u);
    EXPECT_EQ(binder->members[0].path, "bodyShown");
    EXPECT_EQ(binder->members[0].kind, engine::ui::BindKind::Property);
}

TEST(UiLengthBinding, FloatBoundVarPositionsTheElementInPixels) {
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas><Stack class="host"><Stack class="dot" var-x="{binding x}"/></Stack></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = parse_sheet(
            ".host { position: relative; width: 300; height: 100; } "
            ".dot { position: absolute; left: var(--x); top: 4; width: 10; height: 10; }");
    PanelViewModel vm;

    vm.x.set(12.5f);
    bind_and_layout(*parsed, vm, sheet);
    const engine::render::Rect first = find_class(parsed->root, "dot")->layout_rect;
    EXPECT_FLOAT_EQ(first.x, 12.5f);
    EXPECT_FLOAT_EQ(first.y, 4.0f);

    vm.x.set(-3.25f);
    bind_and_layout(*parsed, vm, sheet);
    EXPECT_FLOAT_EQ(find_class(parsed->root, "dot")->layout_rect.x, -3.25f);

    vm.x.set(200.0f);
    bind_and_layout(*parsed, vm, sheet);
    EXPECT_FLOAT_EQ(find_class(parsed->root, "dot")->layout_rect.x, 200.0f);
}

TEST(UiLengthBinding, FloatBoundVarWorksInsideCalcAndForSizes) {
    auto parsed = engine::ui::parse_xml(
            R"(<Canvas><Stack class="host"><Stack class="dot" var-x="{binding x}" var-w="{binding fraction}"/></Stack></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Stylesheet sheet = parse_sheet(
            ".host { position: relative; width: 300; height: 100; } "
            ".dot { position: absolute; left: calc(var(--x) + 5px); top: 0; width: var(--w); height: 10; }");
    PanelViewModel vm;
    vm.x.set(10.0f);
    vm.fraction.set(40.0f);

    bind_and_layout(*parsed, vm, sheet);
    const engine::render::Rect rect = find_class(parsed->root, "dot")->layout_rect;
    EXPECT_FLOAT_EQ(rect.x, 15.0f);
    EXPECT_FLOAT_EQ(rect.w, 40.0f);
}
