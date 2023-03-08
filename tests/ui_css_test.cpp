#include <gtest/gtest.h>

#include <engine/ui/stylesheet.h>

#include <string>
#include <string_view>
#include <vector>

namespace {

const engine::ui::CssRule* find_class_rule(const engine::ui::Stylesheet& sheet, std::string_view class_name) {
    for (const engine::ui::CssRule& rule : sheet.rules) {
        if (rule.selector.type == engine::ui::CssSelectorType::Class && rule.selector.class_name == class_name) {
            return &rule;
        }
    }
    return nullptr;
}

const engine::ui::CssRule* find_element_rule(const engine::ui::Stylesheet& sheet, std::string_view element) {
    for (const engine::ui::CssRule& rule : sheet.rules) {
        if (rule.selector.type == engine::ui::CssSelectorType::Element && rule.selector.element == element) {
            return &rule;
        }
    }
    return nullptr;
}

const engine::ui::CssDeclaration* find_declaration(const engine::ui::CssRule& rule, std::string_view property) {
    for (const engine::ui::CssDeclaration& decl : rule.declarations) {
        if (decl.property == property) {
            return &decl;
        }
    }
    return nullptr;
}

bool warning_mentions(const std::vector<std::string>& warnings, std::string_view token) {
    for (const std::string& warning : warnings) {
        if (warning.find(token) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}

TEST(UiCss, ParseClassAndElementRules) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(R"(
        .hud { padding: 16; gap: 8; flex-direction: vertical; }
        .title { font-size: 24; color: #ffffff; }
        Button { padding: 8 12; border-radius: 4; }
    )",
            warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_TRUE(warnings.empty());
    ASSERT_EQ(sheet->rules.size(), 3u);

    const engine::ui::CssRule* hud = find_class_rule(*sheet, "hud");
    ASSERT_NE(hud, nullptr);
    const engine::ui::CssDeclaration* padding = find_declaration(*hud, "padding");
    ASSERT_NE(padding, nullptr);
    EXPECT_EQ(padding->value, "16");
    const engine::ui::CssDeclaration* gap = find_declaration(*hud, "gap");
    ASSERT_NE(gap, nullptr);
    EXPECT_EQ(gap->value, "8");

    const engine::ui::CssRule* title = find_class_rule(*sheet, "title");
    ASSERT_NE(title, nullptr);
    const engine::ui::CssDeclaration* color = find_declaration(*title, "color");
    ASSERT_NE(color, nullptr);
    EXPECT_EQ(color->value, "#ffffff");

    const engine::ui::CssRule* button = find_element_rule(*sheet, "Button");
    ASSERT_NE(button, nullptr);
    const engine::ui::CssDeclaration* button_padding = find_declaration(*button, "padding");
    ASSERT_NE(button_padding, nullptr);
    EXPECT_EQ(button_padding->value, "8 12");
}

TEST(UiCss, UnknownPropertyWarns) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(".x { color: #ffffff; frobnicate: 1; padding: 4; }", warnings);
    ASSERT_TRUE(sheet.has_value());
    ASSERT_FALSE(warnings.empty());
    EXPECT_TRUE(warning_mentions(warnings, "frobnicate"));

    ASSERT_EQ(sheet->rules.size(), 1u);
    EXPECT_NE(find_declaration(sheet->rules[0], "color"), nullptr);
    EXPECT_NE(find_declaration(sheet->rules[0], "padding"), nullptr);
}

TEST(UiCss, UnknownPropertyDoesNotFailSheet) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(R"(
        Stack Label { color: #ffffff; }
        .ok { padding: 2; zoom: 3; }
    )",
            warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_FALSE(warnings.empty());
    EXPECT_TRUE(warning_mentions(warnings, "zoom") || warning_mentions(warnings, "combinator"));

    const engine::ui::CssRule* ok = find_class_rule(*sheet, "ok");
    ASSERT_NE(ok, nullptr);
    const engine::ui::CssDeclaration* padding = find_declaration(*ok, "padding");
    ASSERT_NE(padding, nullptr);
    EXPECT_EQ(padding->value, "2");
}

