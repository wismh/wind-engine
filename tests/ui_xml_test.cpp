#include <gtest/gtest.h>

#include "ui/bind_scan.h"

#include <engine/resources/fatal_error.h>
#include <engine/ui/document.h>
#include <engine/ui/view_model.h>

#include <optional>
#include <string>
#include <string_view>

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
    engine::ui::Bindable<float> fraction;
    engine::ui::RelayCommand restart;

    HudViewModel() {
        property(engine::ui::intern("title"), title);
        property(engine::ui::intern("restart_label"), restart_label);
        property(engine::ui::intern("fraction"), fraction);
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

}

TEST(UiXml, ParseValidCanvasStackLabelButton) {
    HudViewModel vm;
    vm.title.set("Hello");
    vm.restart_label.set("Again");

    const auto parsed = engine::ui::parse_xml(kValidXml, nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());

    const engine::ui::Element& root = parsed->root;
    EXPECT_EQ(root.kind, engine::ui::ElementKind::Canvas);
    ASSERT_EQ(root.children.size(), 1u);
    EXPECT_EQ(root.children[0].kind, engine::ui::ElementKind::Stack);
    EXPECT_EQ(root.children[0].class_name, "hud");
    EXPECT_EQ(root.children[0].direction, engine::ui::StackDirection::Vertical);
    ASSERT_EQ(root.children[0].children.size(), 2u);
    EXPECT_EQ(root.children[0].children[0].kind, engine::ui::ElementKind::Label);
    EXPECT_EQ(root.children[0].children[0].text_binding, engine::ui::intern("title"));
    EXPECT_EQ(root.children[0].children[1].kind, engine::ui::ElementKind::Button);
    EXPECT_EQ(root.children[0].children[1].command_binding, engine::ui::intern("restart"));
    EXPECT_EQ(root.children[0].children[1].content_binding, engine::ui::intern("restart_label"));
}

TEST(UiXml, UnknownElementIsFatal) {
    RecordingFatalError fatal;
    const auto parsed = engine::ui::parse_xml("<Canvas><Nope/></Canvas>", &fatal);
    EXPECT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), engine::ui::UiError::UnknownElement);
    EXPECT_GE(fatal.call_count, 1);
    EXPECT_NE(fatal.last_message.find("Nope"), std::string::npos);
}

TEST(UiXml, MissingBindingNameIsFatal) {
    HudViewModel vm;
    RecordingFatalError fatal;
    const auto parsed = engine::ui::parse_xml(R"(<Canvas><Label text="{binding score}"/></Canvas>)", &fatal, &vm);
    EXPECT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), engine::ui::UiError::MissingBinding);
    EXPECT_GE(fatal.call_count, 1);
    EXPECT_NE(fatal.last_message.find("score"), std::string::npos);
}

TEST(UiXml, EmptyBindingPathIsFatal) {
    RecordingFatalError fatal;
    const auto parsed = engine::ui::parse_xml(R"(<Canvas><Label text="{binding}"/></Canvas>)", &fatal);
    EXPECT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), engine::ui::UiError::MissingBinding);
    EXPECT_GE(fatal.call_count, 1);
}

TEST(UiXml, OnClickAttributeIsNotAnApi) {
    const auto parsed = engine::ui::parse_xml(R"(<Canvas><Button onClick="nope" content="Go"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Element* button = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::Button);
    ASSERT_NE(button, nullptr);
    EXPECT_FALSE(engine::ui::is_bound(button->command_binding));
    EXPECT_EQ(button->text, "Go");
    EXPECT_EQ(button->command, nullptr);
}

TEST(UiXml, DragAttributeParsesOnAnyElementKind) {
    HudViewModel vm;
    const auto parsed = engine::ui::parse_xml(
            R"(<Canvas><Stack drag="{binding fraction}" drag-orientation="vertical"/></Canvas>)", nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Element* stack = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::Stack);
    ASSERT_NE(stack, nullptr);
    EXPECT_EQ(stack->drag_binding, engine::ui::intern("fraction"));
    EXPECT_EQ(stack->drag_orientation, engine::ui::StackDirection::Vertical);
}

TEST(UiXml, DragOrientationDefaultsHorizontal) {
    HudViewModel vm;
    const auto parsed = engine::ui::parse_xml(R"(<Canvas><Stack drag="{binding fraction}"/></Canvas>)", nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Element* stack = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::Stack);
    ASSERT_NE(stack, nullptr);
    EXPECT_EQ(stack->drag_orientation, engine::ui::StackDirection::Horizontal);
}

TEST(UiXml, DragLiteralValueIsFatal) {
    RecordingFatalError fatal;
    const auto parsed = engine::ui::parse_xml(R"(<Canvas><Stack drag="0.5"/></Canvas>)", &fatal);
    EXPECT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), engine::ui::UiError::MissingBinding);
    EXPECT_GE(fatal.call_count, 1);
}

TEST(UiXml, DragUnregisteredBindingIsFatal) {
    HudViewModel vm;
    RecordingFatalError fatal;
    const auto parsed = engine::ui::parse_xml(R"(<Canvas><Stack drag="{binding nope}"/></Canvas>)", &fatal, &vm);
    EXPECT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), engine::ui::UiError::MissingBinding);
    EXPECT_GE(fatal.call_count, 1);
}

TEST(UiXml, ImageWithSliceParses) {
    const auto parsed = engine::ui::parse_xml(
            R"(<Canvas><Image source="a0e1b2c3d4f5678901234567890abc05" slice="10 12 14 16"/></Canvas>)");
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Element* img = engine::ui::find_by_kind(parsed->root, engine::ui::ElementKind::Image);
    ASSERT_NE(img, nullptr);
    ASSERT_TRUE(img->slice.has_value());
    EXPECT_FLOAT_EQ(img->slice->top.value, 10.f);
    EXPECT_FLOAT_EQ(img->slice->right.value, 12.f);
    EXPECT_FLOAT_EQ(img->slice->bottom.value, 14.f);
    EXPECT_FLOAT_EQ(img->slice->left.value, 16.f);
}

TEST(UiXml, InvalidSliceIsFatal) {
    RecordingFatalError fatal;
    const auto parsed = engine::ui::parse_xml(
            R"(<Canvas><Image source="a0e1b2c3d4f5678901234567890abc05" slice="bad-slice"/></Canvas>)", &fatal);
    EXPECT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), engine::ui::UiError::InvalidMarkup);
    EXPECT_GE(fatal.call_count, 1);
}

TEST(UiXml, ItemTemplateSrcSplicesReferencedRootAsChild) {
    const engine::ui::UiIncludeResolver resolve = [](std::string_view src) -> std::optional<std::string> {
        if (src == "row.xml") {
            return std::string(R"(<Label class="row" content="hello"/>)");
        }
        return std::nullopt;
    };
    const auto parsed =
            engine::ui::parse_xml(R"(<Canvas><ItemTemplate src="row.xml"/></Canvas>)", nullptr, nullptr, resolve);
    ASSERT_TRUE(parsed.has_value());

    const engine::ui::Element& tmpl = parsed->root.children.at(0);
    EXPECT_EQ(tmpl.kind, engine::ui::ElementKind::ItemTemplate);
    ASSERT_EQ(tmpl.children.size(), 1u);
    EXPECT_EQ(tmpl.children[0].kind, engine::ui::ElementKind::Label);
    EXPECT_EQ(tmpl.children[0].class_name, "row");
    EXPECT_EQ(tmpl.children[0].text, "hello");
}

TEST(UiXml, ItemTemplateSrcResolvesNestedIncludes) {
    const engine::ui::UiIncludeResolver resolve = [](std::string_view src) -> std::optional<std::string> {
        if (src == "outer.xml") {
            return std::string(R"(<Stack><ItemTemplate src="inner.xml"/></Stack>)");
        }
        if (src == "inner.xml") {
            return std::string(R"(<Label content="deep"/>)");
        }
        return std::nullopt;
    };
    const auto parsed =
            engine::ui::parse_xml(R"(<Canvas><ItemTemplate src="outer.xml"/></Canvas>)", nullptr, nullptr, resolve);
    ASSERT_TRUE(parsed.has_value());

    const engine::ui::Element& outer_tmpl = parsed->root.children.at(0);
    ASSERT_EQ(outer_tmpl.children.size(), 1u);
    const engine::ui::Element& stack = outer_tmpl.children[0];
    EXPECT_EQ(stack.kind, engine::ui::ElementKind::Stack);
    ASSERT_EQ(stack.children.size(), 1u);
    const engine::ui::Element& inner_tmpl = stack.children[0];
    EXPECT_EQ(inner_tmpl.kind, engine::ui::ElementKind::ItemTemplate);
    ASSERT_EQ(inner_tmpl.children.size(), 1u);
    EXPECT_EQ(inner_tmpl.children[0].text, "deep");
}

TEST(UiXml, ItemTemplateSrcWithoutResolverIsFatal) {
    RecordingFatalError fatal;
    const auto parsed = engine::ui::parse_xml(R"(<Canvas><ItemTemplate src="row.xml"/></Canvas>)", &fatal);
    EXPECT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), engine::ui::UiError::Io);
    EXPECT_GE(fatal.call_count, 1);
}

TEST(UiXml, ItemTemplateSrcMissingFileIsFatal) {
    RecordingFatalError fatal;
    const engine::ui::UiIncludeResolver resolve = [](std::string_view) -> std::optional<std::string> {
        return std::nullopt;
    };
    const auto parsed =
            engine::ui::parse_xml(R"(<Canvas><ItemTemplate src="missing.xml"/></Canvas>)", &fatal, nullptr, resolve);
    EXPECT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), engine::ui::UiError::Io);
}

TEST(UiXml, ItemTemplateSrcInvalidXmlIsFatal) {
    RecordingFatalError fatal;
    const engine::ui::UiIncludeResolver resolve = [](std::string_view) -> std::optional<std::string> {
        return std::string("not xml <<<");
    };
    const auto parsed =
            engine::ui::parse_xml(R"(<Canvas><ItemTemplate src="broken.xml"/></Canvas>)", &fatal, nullptr, resolve);
    EXPECT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), engine::ui::UiError::InvalidMarkup);
}

TEST(UiXml, ItemTemplateSrcWithInlineChildrenIsFatal) {
    RecordingFatalError fatal;
    const engine::ui::UiIncludeResolver resolve = [](std::string_view) -> std::optional<std::string> {
        return std::string(R"(<Label content="hi"/>)");
    };
    const auto parsed = engine::ui::parse_xml(
            R"(<Canvas><ItemTemplate src="row.xml"><Label content="inline"/></ItemTemplate></Canvas>)", &fatal, nullptr,
            resolve);
    EXPECT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), engine::ui::UiError::InvalidMarkup);
}

TEST(UiXml, ItemTemplateSrcSelfIncludeIsFatal) {
    RecordingFatalError fatal;
    const engine::ui::UiIncludeResolver resolve = [](std::string_view src) -> std::optional<std::string> {
        if (src == "self.xml") {
            return std::string(R"(<ItemTemplate src="self.xml"/>)");
        }
        return std::nullopt;
    };
    const auto parsed =
            engine::ui::parse_xml(R"(<Canvas><ItemTemplate src="self.xml"/></Canvas>)", &fatal, nullptr, resolve);
    EXPECT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), engine::ui::UiError::CyclicInclude);
}

TEST(UiXml, ItemTemplateSrcIndirectCycleIsFatal) {
    RecordingFatalError fatal;
    const engine::ui::UiIncludeResolver resolve = [](std::string_view src) -> std::optional<std::string> {
        if (src == "a.xml") {
            return std::string(R"(<ItemTemplate src="b.xml"/>)");
        }
        if (src == "b.xml") {
            return std::string(R"(<ItemTemplate src="a.xml"/>)");
        }
        return std::nullopt;
    };
    const auto parsed =
            engine::ui::parse_xml(R"(<Canvas><ItemTemplate src="a.xml"/></Canvas>)", &fatal, nullptr, resolve);
    EXPECT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), engine::ui::UiError::CyclicInclude);
}

TEST(UiXml, CustomPropertyAttributeParsesAsBinding) {
    HudViewModel vm;
    const auto parsed =
            engine::ui::parse_xml(R"(<Canvas><Label var-tint="{binding title}"/></Canvas>)", nullptr, &vm);
    ASSERT_TRUE(parsed.has_value());
    const engine::ui::Element& label = parsed->root.children.at(0);
    ASSERT_EQ(label.custom_property_bindings.size(), 1u);
    EXPECT_EQ(label.custom_property_bindings[0].name, "tint");
    EXPECT_EQ(label.custom_property_bindings[0].binding, engine::ui::intern("title"));
}

TEST(UiXml, CustomPropertyWithoutBindingIsFatal) {
    RecordingFatalError fatal;
    const auto parsed = engine::ui::parse_xml(R"(<Canvas><Label var-tint="red"/></Canvas>)", &fatal);
    EXPECT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error(), engine::ui::UiError::ForbiddenContent);
    EXPECT_GE(fatal.call_count, 1);
}

TEST(UiXml, ScanBindTreeResolvesBindingsInsideIncludedTemplate) {
    const engine::ui::UiIncludeResolver resolve = [](std::string_view src) -> std::optional<std::string> {
        if (src == "row.xml") {
            return std::string(R"(<Button command="{binding click}" content="{binding mark}"/>)");
        }
        return std::nullopt;
    };
    const auto binder = engine::ui::scan_bind_tree(
            R"(<Canvas><ItemsControl items_source="{binding cells}"><ItemTemplate src="row.xml"/></ItemsControl></Canvas>)",
            resolve);
    ASSERT_TRUE(binder.has_value());
    ASSERT_EQ(binder->nested.size(), 1u);
    EXPECT_EQ(binder->nested[0].first, "cells");
    const engine::ui::BindBinder& item_binder = binder->nested[0].second;
    ASSERT_EQ(item_binder.members.size(), 2u);
    EXPECT_EQ(item_binder.members[0].path, "mark");
    EXPECT_FALSE(item_binder.members[0].is_command);
    EXPECT_EQ(item_binder.members[1].path, "click");
    EXPECT_TRUE(item_binder.members[1].is_command);
}

TEST(UiXml, ScanBindTreeResolvesCustomPropertyAttribute) {
    const auto binder =
            engine::ui::scan_bind_tree(R"(<Canvas><Label var-tint="{binding tint}"/></Canvas>)");
    ASSERT_TRUE(binder.has_value());
    ASSERT_EQ(binder->members.size(), 1u);
    EXPECT_EQ(binder->members[0].path, "tint");
    EXPECT_FALSE(binder->members[0].is_command);
}

