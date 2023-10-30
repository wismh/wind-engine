#include <gtest/gtest.h>

#include <engine/resources/fatal_error.h>
#include <engine/ui/document.h>
#include <engine/ui/view_model.h>

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

