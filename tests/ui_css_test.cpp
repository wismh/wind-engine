#include <gtest/gtest.h>

#include "ui/style_anim.h"

#include <engine/ui/stylesheet.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

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

TEST(UiCss, StylesheetGenerationIsFreshPerConstructionAndKeptByCopy) {
    const engine::ui::Stylesheet first;
    const engine::ui::Stylesheet second;
    EXPECT_GT(second.generation, first.generation);
    const engine::ui::Stylesheet copy = second;
    EXPECT_EQ(copy.generation, second.generation);
    EXPECT_GT(engine::ui::next_stylesheet_generation(), second.generation);
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

TEST(UiCss, ZIndexParsesAsKnownProperty) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(".front { z-index: 5; } .back { z-index: -1; }", warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_TRUE(warnings.empty());

    const engine::ui::CssRule* front = find_class_rule(*sheet, "front");
    ASSERT_NE(front, nullptr);
    const engine::ui::CssDeclaration* front_z = find_declaration(*front, "z-index");
    ASSERT_NE(front_z, nullptr);
    EXPECT_EQ(front_z->value, "5");

    const engine::ui::CssRule* back = find_class_rule(*sheet, "back");
    ASSERT_NE(back, nullptr);
    const engine::ui::CssDeclaration* back_z = find_declaration(*back, "z-index");
    ASSERT_NE(back_z, nullptr);
    EXPECT_EQ(back_z->value, "-1");
}

TEST(UiCss, PositionAndInsetsParseAsKnownProperties) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(
            ".badge { position: absolute; top: 10%; right: 5; bottom: calc(10 + 2); left: 2em; }", warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_TRUE(warnings.empty());

    const engine::ui::CssRule* badge = find_class_rule(*sheet, "badge");
    ASSERT_NE(badge, nullptr);
    EXPECT_NE(find_declaration(*badge, "position"), nullptr);
    EXPECT_EQ(find_declaration(*badge, "position")->value, "absolute");
    EXPECT_NE(find_declaration(*badge, "top"), nullptr);
    EXPECT_NE(find_declaration(*badge, "right"), nullptr);
    EXPECT_NE(find_declaration(*badge, "bottom"), nullptr);
    EXPECT_NE(find_declaration(*badge, "left"), nullptr);
}

TEST(UiCss, VarReferenceInLengthPropertyParsesWithoutWarning) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(
            ".popup { position: absolute; top: var(--spawn-y); left: var(--spawn-x, 0); width: var(--w); }",
            warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_TRUE(warnings.empty());

    const engine::ui::CssRule* popup = find_class_rule(*sheet, "popup");
    ASSERT_NE(popup, nullptr);
    const engine::ui::CssDeclaration* top = find_declaration(*popup, "top");
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->value, "var(--spawn-y)");
    const engine::ui::CssDeclaration* left = find_declaration(*popup, "left");
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->value, "var(--spawn-x, 0)");
}

TEST(UiCss, VarReferenceInBackgroundSliceParsesWithoutWarning) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(".panel { background-slice: var(--slice); }", warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_TRUE(warnings.empty());

    const engine::ui::CssRule* panel = find_class_rule(*sheet, "panel");
    ASSERT_NE(panel, nullptr);
    EXPECT_NE(find_declaration(*panel, "background-slice"), nullptr);
}

TEST(UiCss, TransformParsesAsKnownProperty) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(".spin { transform: rotate(45) scale(1.5); }", warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_TRUE(warnings.empty());

    const engine::ui::CssRule* spin = find_class_rule(*sheet, "spin");
    ASSERT_NE(spin, nullptr);
    const engine::ui::CssDeclaration* transform = find_declaration(*spin, "transform");
    ASSERT_NE(transform, nullptr);
    EXPECT_EQ(transform->value, "rotate(45) scale(1.5)");
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

TEST(UiCss, WarningsNameTheLineOfTheDeclaration) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(".x {\n    color: #ffffff;\n    frobnicate: 1;\n}\n"
                                             "@media (wobble) {\n}\n.y + .z { color: #000000; }\n",
            warnings);
    ASSERT_TRUE(sheet.has_value());
    ASSERT_EQ(warnings.size(), 3u);
    EXPECT_EQ(warnings[0], "line 3: unknown CSS property: frobnicate");
    EXPECT_EQ(warnings[1], "line 5: unknown media");
    EXPECT_EQ(warnings[2], "line 7: unsupported combinator in selector: .y + .z");
}

TEST(UiCss, UnknownPropertyDoesNotFailSheet) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(R"(
        Stack Label { color: #ffffff; }
        .ok { padding: 2; zoom: 3; }
    )",
            warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_TRUE(warning_mentions(warnings, "zoom"));
    EXPECT_FALSE(warning_mentions(warnings, "combinator"));

    const engine::ui::CssRule* ok = find_class_rule(*sheet, "ok");
    ASSERT_NE(ok, nullptr);
    const engine::ui::CssDeclaration* padding = find_declaration(*ok, "padding");
    ASSERT_NE(padding, nullptr);
    EXPECT_EQ(padding->value, "2");
}

TEST(UiCss, TextAlignIsKnownProperty) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(".x { text-align: center; frobnicate: 1; }", warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_FALSE(warning_mentions(warnings, "text-align"));
    EXPECT_TRUE(warning_mentions(warnings, "frobnicate"));
    ASSERT_EQ(sheet->rules.size(), 1u);
    const engine::ui::CssDeclaration* text_align = find_declaration(sheet->rules[0], "text-align");
    ASSERT_NE(text_align, nullptr);
    EXPECT_EQ(text_align->value, "center");
}

TEST(UiCss, LineHeightIsKnownProperty) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(".x { line-height: 1.5; frobnicate: 1; }", warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_FALSE(warning_mentions(warnings, "line-height"));
    EXPECT_TRUE(warning_mentions(warnings, "frobnicate"));
    ASSERT_EQ(sheet->rules.size(), 1u);
    const engine::ui::CssDeclaration* line_height = find_declaration(sheet->rules[0], "line-height");
    ASSERT_NE(line_height, nullptr);
    EXPECT_EQ(line_height->value, "1.5");
}

TEST(UiCss, WhiteSpaceAndMaxWidthAreKnownProperties) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(".x { white-space: nowrap; max-width: 120px; }", warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_FALSE(warning_mentions(warnings, "white-space"));
    EXPECT_FALSE(warning_mentions(warnings, "max-width"));
    ASSERT_EQ(sheet->rules.size(), 1u);
    const engine::ui::CssDeclaration* white_space = find_declaration(sheet->rules[0], "white-space");
    ASSERT_NE(white_space, nullptr);
    EXPECT_EQ(white_space->value, "nowrap");
    EXPECT_NE(find_declaration(sheet->rules[0], "max-width"), nullptr);
}

TEST(UiCss, BackgroundImageIsKnownProperty) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(
            ".x { background-image: c1a1c2d3e4f5678901234567890abc0a; frobnicate: 1; }", warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_FALSE(warning_mentions(warnings, "background-image"));
    EXPECT_TRUE(warning_mentions(warnings, "frobnicate"));
    ASSERT_EQ(sheet->rules.size(), 1u);
    const engine::ui::CssDeclaration* background_image = find_declaration(sheet->rules[0], "background-image");
    ASSERT_NE(background_image, nullptr);
    EXPECT_EQ(background_image->value, "c1a1c2d3e4f5678901234567890abc0a");
}

TEST(UiCss, BackgroundRepeatIsKnownProperty) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(".x { background-repeat: repeat; }", warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_FALSE(warning_mentions(warnings, "background-repeat"));
    ASSERT_EQ(sheet->rules.size(), 1u);
    const engine::ui::CssDeclaration* background_repeat = find_declaration(sheet->rules[0], "background-repeat");
    ASSERT_NE(background_repeat, nullptr);
    EXPECT_EQ(background_repeat->value, "repeat");
}

TEST(UiCss, LinePropertiesAreKnown) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(
            ".x { x1: 0; y1: 0; x2: 40; y2: 40; stroke: #ff0000; stroke-width: 3; }", warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_FALSE(warning_mentions(warnings, "x1"));
    EXPECT_FALSE(warning_mentions(warnings, "y1"));
    EXPECT_FALSE(warning_mentions(warnings, "x2"));
    EXPECT_FALSE(warning_mentions(warnings, "y2"));
    EXPECT_FALSE(warning_mentions(warnings, "stroke"));
    ASSERT_EQ(sheet->rules.size(), 1u);
    const engine::ui::CssDeclaration* x2 = find_declaration(sheet->rules[0], "x2");
    ASSERT_NE(x2, nullptr);
    EXPECT_EQ(x2->value, "40");
    const engine::ui::CssDeclaration* stroke_width = find_declaration(sheet->rules[0], "stroke-width");
    ASSERT_NE(stroke_width, nullptr);
    EXPECT_EQ(stroke_width->value, "3");
}

TEST(UiCss, BackgroundImageFilenameWarns) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css("Button { background-image: hover.png; }", warnings);
    ASSERT_TRUE(sheet.has_value());
    ASSERT_FALSE(warnings.empty());
    EXPECT_TRUE(warning_mentions(warnings, "hover.png"));
    EXPECT_TRUE(warning_mentions(warnings, "background-image"));
}

TEST(UiCss, DescendantAndChildCombinatorsDoNotWarn) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(R"(
        Stack.hud Label { color: #ffffff; }
        Stack > Label { padding: 4; }
    )",
            warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_FALSE(warning_mentions(warnings, "combinator"));
    ASSERT_EQ(sheet->rules.size(), 2u);

    EXPECT_EQ(sheet->rules[0].ancestors.size(), 1u);
    EXPECT_EQ(sheet->rules[0].combinators.size(), 1u);
    EXPECT_EQ(sheet->rules[0].ancestors[0].type, engine::ui::CssSelectorType::ElementClass);
    EXPECT_EQ(sheet->rules[0].ancestors[0].element, "Stack");
    EXPECT_EQ(sheet->rules[0].ancestors[0].class_name, "hud");
    EXPECT_EQ(sheet->rules[0].combinators[0], engine::ui::CssCombinator::Descendant);
    EXPECT_EQ(sheet->rules[0].selector.type, engine::ui::CssSelectorType::Element);
    EXPECT_EQ(sheet->rules[0].selector.element, "Label");

    EXPECT_EQ(sheet->rules[1].ancestors.size(), 1u);
    EXPECT_EQ(sheet->rules[1].combinators[0], engine::ui::CssCombinator::Child);
    EXPECT_EQ(sheet->rules[1].selector.element, "Label");
}

TEST(UiCss, AdjacentSiblingCombinatorWarns) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css("Label + Button { color: #ffffff; }", warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_TRUE(warning_mentions(warnings, "combinator"));
    EXPECT_TRUE(sheet->rules.empty());
}

TEST(UiCss, BackgroundSliceIsKnownProperty) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(R"(
        .panel {
            background-slice: 12;
            background-slice: 8 16;
            background-slice: 10 12 14 16;
        }
    )",
            warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_TRUE(warnings.empty());
    const engine::ui::CssRule* panel = find_class_rule(*sheet, "panel");
    ASSERT_NE(panel, nullptr);
    ASSERT_EQ(panel->declarations.size(), 3u);
    EXPECT_EQ(panel->declarations[0].property, "background-slice");
    EXPECT_EQ(panel->declarations[0].value, "12");
}

TEST(UiCss, InvalidBackgroundSliceWarns) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(R"(
        .panel {
            background-slice: invalid-val;
            background-slice: 1 2 3 4 5;
        }
    )",
            warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_FALSE(warnings.empty());
    EXPECT_TRUE(warning_mentions(warnings, "invalid background-slice"));
}

namespace {

engine::ui::ComputedStyle apply_motion_rule(const engine::ui::CssRule& rule) {
    engine::ui::ComputedStyle style;
    for (const engine::ui::CssDeclaration& decl : rule.declarations) {
        engine::ui::apply_motion_declaration(style, decl.property, decl.value);
    }
    return style;
}

const engine::ui::ShownMotion* find_shown(const engine::ui::Element& element, engine::ui::MotionProp prop) {
    for (const engine::ui::ShownMotion& shown : element.motion_shown) {
        if (shown.prop == prop) {
            return &shown;
        }
    }
    return nullptr;
}

}  // namespace

TEST(UiCss, TransitionAndAnimationShorthandListsAndInfinite) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(R"(
        .a {
            transition: opacity 0.2s ease 0.1s, color 1s;
            animation: fade 1s ease-in 0s infinite;
        }
        .c {
            transition-property: opacity, color;
            transition-duration: 0.2s;
            transition-delay: 0s, 0.5s;
            transition-timing-function: linear;
            animation-name: fade, spin;
            animation-duration: 1s, 2s;
            animation-delay: 0.25s;
            animation-timing-function: ease-out;
            animation-iteration-count: 3, infinite;
        }
    )",
            warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_TRUE(warnings.empty());

    const engine::ui::CssRule* shorthand = find_class_rule(*sheet, "a");
    ASSERT_NE(shorthand, nullptr);
    const engine::ui::ComputedStyle expanded = apply_motion_rule(*shorthand);
    ASSERT_EQ(expanded.transition_properties.size(), 2u);
    EXPECT_EQ(expanded.transition_properties[0], "opacity");
    EXPECT_EQ(expanded.transition_properties[1], "color");
    ASSERT_EQ(expanded.transition_durations.size(), 2u);
    EXPECT_FLOAT_EQ(expanded.transition_durations[0], 0.2f);
    EXPECT_FLOAT_EQ(expanded.transition_durations[1], 1.0f);
    ASSERT_EQ(expanded.transition_delays.size(), 2u);
    EXPECT_FLOAT_EQ(expanded.transition_delays[0], 0.1f);
    EXPECT_FLOAT_EQ(expanded.transition_delays[1], 0.0f);
    ASSERT_EQ(expanded.transition_easings.size(), 2u);
    EXPECT_EQ(expanded.transition_easings[0], engine::ui::CssEasing::Ease);
    EXPECT_EQ(expanded.transition_easings[1], engine::ui::CssEasing::Linear);
    ASSERT_EQ(expanded.animation_names.size(), 1u);
    EXPECT_EQ(expanded.animation_names[0], "fade");
    ASSERT_EQ(expanded.animation_durations.size(), 1u);
    EXPECT_FLOAT_EQ(expanded.animation_durations[0], 1.0f);
    ASSERT_EQ(expanded.animation_delays.size(), 1u);
    EXPECT_FLOAT_EQ(expanded.animation_delays[0], 0.0f);
    ASSERT_EQ(expanded.animation_easings.size(), 1u);
    EXPECT_EQ(expanded.animation_easings[0], engine::ui::CssEasing::EaseIn);
    ASSERT_EQ(expanded.animation_iterations.size(), 1u);
    EXPECT_FLOAT_EQ(expanded.animation_iterations[0], -1.0f);

    const engine::ui::CssRule* lists = find_class_rule(*sheet, "c");
    ASSERT_NE(lists, nullptr);
    const engine::ui::ComputedStyle longhands = apply_motion_rule(*lists);
    ASSERT_EQ(longhands.transition_properties.size(), 2u);
    EXPECT_EQ(longhands.transition_properties[0], "opacity");
    EXPECT_EQ(longhands.transition_properties[1], "color");
    ASSERT_EQ(longhands.transition_durations.size(), 1u);
    EXPECT_FLOAT_EQ(longhands.transition_durations[0], 0.2f);
    ASSERT_EQ(longhands.transition_delays.size(), 2u);
    EXPECT_FLOAT_EQ(longhands.transition_delays[1], 0.5f);
    ASSERT_EQ(longhands.animation_names.size(), 2u);
    EXPECT_EQ(longhands.animation_names[1], "spin");
    ASSERT_EQ(longhands.animation_durations.size(), 2u);
    EXPECT_FLOAT_EQ(longhands.animation_durations[1], 2.0f);
    ASSERT_EQ(longhands.animation_iterations.size(), 2u);
    EXPECT_FLOAT_EQ(longhands.animation_iterations[0], 3.0f);
    EXPECT_FLOAT_EQ(longhands.animation_iterations[1], -1.0f);
    ASSERT_EQ(longhands.animation_easings.size(), 1u);
    EXPECT_EQ(longhands.animation_easings[0], engine::ui::CssEasing::EaseOut);
}

TEST(UiCss, UnknownEasingWarns) {
    std::vector<std::string> warnings;
    const auto sheet = engine::ui::parse_css(R"(
        .b { transition-timing-function: bounce; }
        .d { animation: fade 1s bounce; }
        .e { transition: opacity 1s wobble; }
    )",
            warnings);
    ASSERT_TRUE(sheet.has_value());
    EXPECT_TRUE(warning_mentions(warnings, "unknown easing"));
    EXPECT_TRUE(warning_mentions(warnings, "bounce"));
    EXPECT_TRUE(warning_mentions(warnings, "wobble"));
    const engine::ui::CssRule* timing = find_class_rule(*sheet, "b");
    ASSERT_NE(timing, nullptr);
    EXPECT_NE(find_declaration(*timing, "transition-timing-function"), nullptr);
}

TEST(UiCss, TransitionListRepeatsLastAndDropsExtras) {
    engine::ui::ComputedStyle style;
    engine::ui::apply_motion_declaration(style, "transition-property", "opacity, background, color");
    engine::ui::apply_motion_declaration(style, "transition-duration", "1s, 0s");
    style.opacity = 0.0f;
    style.background = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
    style.color = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};

    engine::ui::Element element;
    engine::ui::advance_motion(element, style, nullptr, 0.0f, glm::vec2{100.0f, 100.0f});
    style.opacity = 1.0f;
    style.background = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
    style.color = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
    engine::ui::advance_motion(element, style, nullptr, 0.5f, glm::vec2{100.0f, 100.0f});

    const engine::ui::ShownMotion* opacity = find_shown(element, engine::ui::MotionProp::Opacity);
    ASSERT_NE(opacity, nullptr);
    EXPECT_NEAR(opacity->value.number, 0.5f, 0.02f);
    // The short duration list repeats its last value (0s), so both later properties snap to the goal.
    // A transition already at its goal is not stored in motion_shown; the player still holds the snap.
    EXPECT_EQ(find_shown(element, engine::ui::MotionProp::Background), nullptr);
    EXPECT_EQ(find_shown(element, engine::ui::MotionProp::Color), nullptr);
    const engine::ui::TransitionRuntime* background = nullptr;
    const engine::ui::TransitionRuntime* color = nullptr;
    for (const engine::ui::TransitionRuntime& player : element.transition_players) {
        if (player.prop == engine::ui::MotionProp::Background) {
            background = &player;
        } else if (player.prop == engine::ui::MotionProp::Color) {
            color = &player;
        }
    }
    ASSERT_NE(background, nullptr);
    ASSERT_NE(color, nullptr);
    EXPECT_FALSE(background->running);
    EXPECT_FALSE(color->running);
    EXPECT_NEAR(background->shown.color.r, 1.0f, 0.02f);
    EXPECT_NEAR(color->shown.color.r, 1.0f, 0.02f);

    engine::ui::ComputedStyle extras;
    engine::ui::apply_motion_declaration(extras, "transition-property", "opacity, background");
    engine::ui::apply_motion_declaration(extras, "transition-duration", "1s, 2s, 9s");
    extras.opacity = 0.0f;
    extras.background = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
    engine::ui::Element paired;
    engine::ui::advance_motion(paired, extras, nullptr, 0.0f, glm::vec2{100.0f, 100.0f});
    extras.opacity = 1.0f;
    extras.background = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
    engine::ui::advance_motion(paired, extras, nullptr, 0.5f, glm::vec2{100.0f, 100.0f});
    const engine::ui::ShownMotion* paired_opacity = find_shown(paired, engine::ui::MotionProp::Opacity);
    const engine::ui::ShownMotion* paired_background = find_shown(paired, engine::ui::MotionProp::Background);
    ASSERT_NE(paired_opacity, nullptr);
    ASSERT_NE(paired_background, nullptr);
    EXPECT_NEAR(paired_opacity->value.number, 0.5f, 0.02f);
    EXPECT_NEAR(paired_background->value.color.r, 0.25f, 0.02f);
}

