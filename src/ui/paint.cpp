#include "painter.h"
#include "css_length.h"
#include "draw_list_adapter.h"
#include "math/math_element.h"

#include <engine/builtin_ids.h>
#include <engine/ui/canvas.h>

#include <glm/vec2.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::ui {
namespace {

// BackgroundRepeat and ComputedStyle now live in <engine/ui/document.h> (still engine::ui-scoped,
// found here by unqualified lookup) so Element can cache a compute_style() result — see
// StyleCacheEntry there.

std::string_view trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

const char* kind_name(ElementKind kind) {
    switch (kind) {
        case ElementKind::Canvas:
            return "Canvas";
        case ElementKind::Stack:
            return "Stack";
        case ElementKind::Label:
            return "Label";
        case ElementKind::Button:
            return "Button";
        case ElementKind::Image:
            return "Image";
        case ElementKind::ItemsControl:
            return "ItemsControl";
        case ElementKind::ItemTemplate:
            return "ItemTemplate";
        case ElementKind::Line:
            return "Line";
        case ElementKind::Component:
            return "Component";
        case ElementKind::Viewport:
            return "Viewport";
        case ElementKind::TextInput:
            return "TextInput";
        case ElementKind::ScrollView:
            return "ScrollView";
        case ElementKind::Checkbox:
            return "Checkbox";
        case ElementKind::Math:
            return "Math";
    }
    return "";
}

int hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return 0;
}

int hex_byte(char hi, char lo) {
    return (hex_nibble(hi) << 4) | hex_nibble(lo);
}

std::optional<glm::vec4> parse_color(std::string_view raw) {
    const std::string_view value = trim(raw);
    if (value.empty() || value.front() != '#') {
        return std::nullopt;
    }
    const std::string_view hex = value.substr(1);
    if (hex.size() == 3) {
        const float r = static_cast<float>(hex_nibble(hex[0])) / 15.0f;
        const float g = static_cast<float>(hex_nibble(hex[1])) / 15.0f;
        const float b = static_cast<float>(hex_nibble(hex[2])) / 15.0f;
        return glm::vec4{r, g, b, 1.0f};
    }
    if (hex.size() == 6) {
        return glm::vec4{
                static_cast<float>(hex_byte(hex[0], hex[1])) / 255.0f,
                static_cast<float>(hex_byte(hex[2], hex[3])) / 255.0f,
                static_cast<float>(hex_byte(hex[4], hex[5])) / 255.0f,
                1.0f,
        };
    }
    if (hex.size() == 8) {
        return glm::vec4{
                static_cast<float>(hex_byte(hex[0], hex[1])) / 255.0f,
                static_cast<float>(hex_byte(hex[2], hex[3])) / 255.0f,
                static_cast<float>(hex_byte(hex[4], hex[5])) / 255.0f,
                static_cast<float>(hex_byte(hex[6], hex[7])) / 255.0f,
        };
    }
    return std::nullopt;
}

std::optional<float> parse_percent(std::string_view raw) {
    const std::string_view value = trim(raw);
    if (value.empty()) {
        return std::nullopt;
    }
    const std::string tmp(value);
    char* end = nullptr;
    const float n = std::strtof(tmp.c_str(), &end);
    if (end == tmp.c_str()) {
        return std::nullopt;
    }
    const std::string_view suffix = trim(std::string_view(end));
    if (suffix != "%") {
        return std::nullopt;
    }
    return n;
}

std::optional<float> parse_angle_deg(std::string_view raw) {
    const std::string_view value = trim(raw);
    if (value.empty()) {
        return std::nullopt;
    }
    const std::string tmp(value);
    char* end = nullptr;
    const float n = std::strtof(tmp.c_str(), &end);
    if (end == tmp.c_str()) {
        return std::nullopt;
    }
    const std::string_view suffix = trim(std::string_view(end));
    if (!suffix.empty() && suffix != "deg") {
        return std::nullopt;
    }
    return n;
}

// Gradient stops never nest another `(...)`, so a plain scan (no paren-depth tracking) is enough
// here, unlike a general CSS value parser.
std::vector<std::string_view> split_top_level(std::string_view value, char sep) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= value.size(); ++i) {
        if (i == value.size() || value[i] == sep) {
            parts.push_back(trim(value.substr(start, i - start)));
            start = i + 1;
        }
    }
    return parts;
}

std::vector<std::string_view> split_whitespace(std::string_view value) {
    std::vector<std::string_view> parts;
    std::size_t i = 0;
    while (i < value.size()) {
        while (i < value.size() && std::isspace(static_cast<unsigned char>(value[i])) != 0) {
            ++i;
        }
        const std::size_t start = i;
        while (i < value.size() && std::isspace(static_cast<unsigned char>(value[i])) == 0) {
            ++i;
        }
        if (i > start) {
            parts.push_back(value.substr(start, i - start));
        }
    }
    return parts;
}

// Distributes any stop with no explicit percent evenly across [0,100] by index — a documented
// simplification of CSS's partial-specification spacing rule (which only interpolates the gaps
// between explicitly-positioned neighbors); good enough for the two shapes a gradient is actually
// authored in here: every stop positioned, or none of them.
void fill_missing_stop_percents(std::vector<GradientStop>& stops) {
    if (stops.size() < 2) {
        return;
    }
    for (std::size_t i = 0; i < stops.size(); ++i) {
        if (!stops[i].percent) {
            stops[i].percent = 100.0f * static_cast<float>(i) / static_cast<float>(stops.size() - 1);
        }
    }
}

// Each stop is `<#hexcolor> [<pct>%] [<pct>%]?` — the second percent is CSS's hard-stop shorthand
// (the same color repeated at two offsets, e.g. a progress ring's filled wedge in one stop entry).
bool parse_gradient_stops(std::string_view body, std::vector<GradientStop>& stops) {
    for (const std::string_view raw_stop : split_top_level(body, ',')) {
        const std::vector<std::string_view> parts = split_whitespace(raw_stop);
        if (parts.empty() || parts.size() > 3) {
            return false;
        }
        const auto color = parse_color(parts[0]);
        if (!color) {
            return false;
        }
        if (parts.size() == 1) {
            stops.push_back(GradientStop{*color, std::nullopt});
            continue;
        }
        const auto pct1 = parse_percent(parts[1]);
        if (!pct1) {
            return false;
        }
        stops.push_back(GradientStop{*color, *pct1});
        if (parts.size() == 3) {
            const auto pct2 = parse_percent(parts[2]);
            if (!pct2) {
                return false;
            }
            stops.push_back(GradientStop{*color, *pct2});
        }
    }
    if (stops.size() < 2) {
        return false;
    }
    fill_missing_stop_percents(stops);
    return true;
}

// linear-gradient([<angle>deg ,] <stop-list>) | radial-gradient(<stop-list>) |
// conic-gradient(<stop-list>). Not a general CSS <gradient> grammar: no `to <side>` keyword angles,
// no explicit radial/conic shape or position arguments, no rgba()/named colors (parse_color is
// hex-only, same limitation every other color property already has here). Angle/percent tokens are
// plain numbers with an optional deg/% suffix, matching every other length-ish value in this parser.
std::optional<Gradient> parse_gradient(std::string_view raw) {
    const std::string_view value = trim(raw);
    GradientKind kind;
    std::string_view prefix;
    if (value.starts_with("linear-gradient(")) {
        kind = GradientKind::Linear;
        prefix = "linear-gradient(";
    } else if (value.starts_with("radial-gradient(")) {
        kind = GradientKind::Radial;
        prefix = "radial-gradient(";
    } else if (value.starts_with("conic-gradient(")) {
        kind = GradientKind::Conic;
        prefix = "conic-gradient(";
    } else {
        return std::nullopt;
    }
    if (!value.ends_with(')')) {
        return std::nullopt;
    }
    std::string_view body = value.substr(prefix.size(), value.size() - prefix.size() - 1);

    Gradient gradient;
    gradient.kind = kind;
    if (kind == GradientKind::Linear) {
        const std::size_t comma = body.find(',');
        const std::string_view first = trim(comma == std::string_view::npos ? body : body.substr(0, comma));
        if (const auto angle = parse_angle_deg(first)) {
            gradient.angle_deg = *angle;
            if (comma == std::string_view::npos) {
                return std::nullopt;  // angle with no stops after it
            }
            body = body.substr(comma + 1);
        }
    }
    if (!parse_gradient_stops(body, gradient.stops)) {
        return std::nullopt;
    }
    return gradient;
}

UiAlign parse_align(std::string_view raw) {
    const std::string_view value = trim(raw);
    if (value == "center") {
        return UiAlign::Center;
    }
    if (value == "end" || value == "flex-end") {
        return UiAlign::End;
    }
    if (value == "space-between") {
        return UiAlign::SpaceBetween;
    }
    return UiAlign::Start;
}

WhiteSpace parse_white_space(std::string_view raw) {
    return trim(raw) == "nowrap" ? WhiteSpace::NoWrap : WhiteSpace::Normal;
}

// A bare number is a unitless factor, not px (unlike font-size). 0 and negative are invalid.
std::optional<LineHeight> parse_line_height(std::string_view raw) {
    const std::string_view value = trim(raw);
    if (value == "normal") {
        return LineHeight{};
    }
    const std::string tmp(value);
    char* end = nullptr;
    const float n = std::strtof(tmp.c_str(), &end);
    if (end == tmp.c_str() || !(n > 0.0f)) {
        return std::nullopt;
    }
    std::size_t i = static_cast<std::size_t>(end - tmp.c_str());
    const std::size_t unit_begin = i;
    while (i < tmp.size() && std::isspace(static_cast<unsigned char>(tmp[i])) == 0) {
        ++i;
    }
    const std::string_view suffix(tmp.data() + unit_begin, i - unit_begin);
    while (i < tmp.size()) {
        if (std::isspace(static_cast<unsigned char>(tmp[i])) == 0) {
            return std::nullopt;
        }
        ++i;
    }
    LineHeight parsed;
    if (suffix.empty()) {
        parsed.kind = LineHeightKind::Factor;
        parsed.factor = n;
        return parsed;
    }
    parsed.kind = LineHeightKind::Length;
    parsed.length.value = n;
    if (suffix == "px") {
        parsed.length.unit = LengthUnit::Px;
    } else if (suffix == "em") {
        parsed.length.unit = LengthUnit::Em;
    } else if (suffix == "%") {
        parsed.length.unit = LengthUnit::Percent;
    } else {
        return std::nullopt;
    }
    return parsed;
}

UiAlign parse_text_align(std::string_view raw) {
    const std::string_view value = trim(raw);
    if (value == "center") {
        return UiAlign::Center;
    }
    if (value == "end") {
        return UiAlign::End;
    }
    return UiAlign::Start;
}

PositionMode parse_position(std::string_view raw) {
    const std::string_view value = trim(raw);
    if (value == "relative") {
        return PositionMode::Relative;
    }
    if (value == "absolute") {
        return PositionMode::Absolute;
    }
    return PositionMode::Static;
}

BackgroundRepeat parse_background_repeat(std::string_view raw) {
    const std::string_view value = trim(raw);
    if (value == "repeat") {
        return BackgroundRepeat::Repeat;
    }
    return BackgroundRepeat::NoRepeat;
}

struct ParsedTransform {
    float rotation_deg = 0.0f;
    float scale = 1.0f;
};

// Deliberately not a general CSS transform-function grammar: only rotate(N) / rotate(Ndeg) and
// scale(N), order-independent (this engine supports rotation+scale about the element's own
// center, not a composed matrix where function order would matter). A malformed or unrecognized
// function is silently ignored, same as the parser's "unknown property" warn-and-continue.
std::optional<float> parse_transform_arg(std::string_view value, std::string_view fn_name) {
    const std::size_t start = value.find(fn_name);
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t open = value.find('(', start);
    if (open == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t close = value.find(')', open);
    if (close == std::string_view::npos || close <= open) {
        return std::nullopt;
    }
    const std::string arg(trim(value.substr(open + 1, close - open - 1)));
    if (arg.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const float parsed = std::strtof(arg.c_str(), &end);
    if (end == arg.c_str()) {
        return std::nullopt;
    }
    return parsed;
}

ParsedTransform parse_transform(std::string_view value) {
    ParsedTransform result;
    if (const auto rotation = parse_transform_arg(value, "rotate")) {
        result.rotation_deg = *rotation;
    }
    if (const auto scale = parse_transform_arg(value, "scale")) {
        result.scale = *scale;
    }
    return result;
}

std::optional<float> parse_seconds(std::string_view raw) {
    const std::string_view value = trim(raw);
    if (value.empty()) {
        return std::nullopt;
    }
    const std::string tmp(value);
    char* end = nullptr;
    const float n = std::strtof(tmp.c_str(), &end);
    if (end == tmp.c_str()) {
        return std::nullopt;
    }
    const std::string_view suffix = trim(std::string_view(end));
    if (suffix.empty() || suffix == "s") {
        return n;
    }
    return std::nullopt;
}

bool has_class(const Element& element, const std::string& class_name) {
    return std::ranges::find(element.classes, class_name) != element.classes.end();
}

bool compound_matches(const CssSelector& selector, const Element& element) {
    switch (selector.type) {
        case CssSelectorType::Element:
            return kind_name(element.kind) == selector.element;
        case CssSelectorType::Class:
            return has_class(element, selector.class_name);
        case CssSelectorType::Id:
            return element.id == selector.id;
        case CssSelectorType::ElementClass:
            return kind_name(element.kind) == selector.element && has_class(element, selector.class_name);
    }
    return false;
}

bool subject_matches(const CssSelector& selector, const Element& element, bool allow_pseudo) {
    if (!compound_matches(selector, element)) {
        return false;
    }
    if (selector.pseudo.empty()) {
        return true;
    }
    if (!allow_pseudo) {
        return false;
    }
    if (selector.pseudo == "hover") {
        return element.hovered;
    }
    if (selector.pseudo == "pressed") {
        return element.pressed;
    }
    if (selector.pseudo == "disabled") {
        return element.disabled;
    }
    if (selector.pseudo == "focus") {
        return element.focused;
    }
    if (selector.pseudo == "checked") {
        return element.checked;
    }
    return false;
}

bool selector_matches(
        const CssRule& rule, const Element& element, const std::vector<const Element*>& ancestors, bool allow_pseudo) {
    if (!subject_matches(rule.selector, element, allow_pseudo)) {
        return false;
    }
    if (rule.ancestors.size() != rule.combinators.size()) {
        return false;
    }
    std::size_t pos = ancestors.size();
    for (std::size_t n = rule.ancestors.size(); n > 0; --n) {
        const std::size_t i = n - 1;
        const CssSelector& compound = rule.ancestors[i];
        if (rule.combinators[i] == CssCombinator::Child) {
            if (pos == 0) {
                return false;
            }
            --pos;
            if (!subject_matches(compound, *ancestors[pos], allow_pseudo)) {
                return false;
            }
        } else {
            bool found = false;
            while (pos > 0) {
                --pos;
                if (subject_matches(compound, *ancestors[pos], allow_pseudo)) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
    }
    return true;
}

// Specificity is the sum of every compound in the chain (ancestors + subject).
// Per compound: element=1, class=2, ElementClass=3, id=4. Subject pseudo-class adds +10.
// Example: Stack.hud Label = 3 + 1 = 4. Equal scores keep later rule index.
int compound_specificity(const CssSelector& selector) {
    int score = 0;
    switch (selector.type) {
        case CssSelectorType::Element:
            score = 1;
            break;
        case CssSelectorType::Class:
            score = 2;
            break;
        case CssSelectorType::ElementClass:
            score = 3;
            break;
        case CssSelectorType::Id:
            score = 4;
            break;
    }
    if (!selector.pseudo.empty()) {
        score += 10;
    }
    return score;
}

int specificity(const CssRule& rule) {
    int score = compound_specificity(rule.selector);
    for (const CssSelector& ancestor : rule.ancestors) {
        score += compound_specificity(ancestor);
    }
    return score;
}

void apply_declaration(ComputedStyle& style, const CssDeclaration& decl) {
    if (decl.property == "color") {
        if (const auto color = parse_color(decl.value)) {
            style.color = *color;
        }
    } else if (decl.property == "background") {
        if (const auto gradient = parse_gradient(decl.value)) {
            style.background_gradient = *gradient;
            style.background_image.reset();
        } else if (const auto color = parse_color(decl.value)) {
            style.background = *color;
        }
    } else if (decl.property == "background-image") {
        const std::string_view value = trim(decl.value);
        if (const auto gradient = parse_gradient(value)) {
            style.background_gradient = *gradient;
            style.background_image.reset();
        } else if (value == "none") {
            style.background_image.reset();
            style.background_gradient.reset();
        } else if (const auto id = AssetId::parse(value)) {
            style.background_image = *id;
            style.background_gradient.reset();
        }
    } else if (decl.property == "background-slice") {
        if (const auto insets = css_length::parse_insets(decl.value)) {
            style.background_slice = *insets;
        }
    } else if (decl.property == "background-repeat") {
        style.background_repeat = parse_background_repeat(decl.value);
    } else if (decl.property == "opacity") {
        style.opacity = std::strtof(decl.value.c_str(), nullptr);
    } else if (decl.property == "visibility") {
        style.visible = trim(decl.value) != "hidden";
    } else if (decl.property == "display") {
        // Only `none` is supported; any other value is the default (displayed).
        style.display_none = trim(decl.value) == "none";
    } else if (decl.property == "gap") {
        if (const auto gap = css_length::parse_length(decl.value)) {
            style.gap = *gap;
            style.has_gap = true;
        }
    } else if (decl.property == "flex-direction") {
        const std::string_view value = trim(decl.value);
        style.has_direction = true;
        if (value == "horizontal" || value == "row") {
            style.direction = StackDirection::Horizontal;
        } else {
            style.direction = StackDirection::Vertical;
        }
    } else if (decl.property == "padding") {
        if (const auto padding = css_length::parse_insets(decl.value)) {
            style.padding = *padding;
        }
    } else if (decl.property == "margin") {
        if (const auto margin = css_length::parse_insets(decl.value)) {
            style.margin = *margin;
        }
    } else if (decl.property == "width") {
        style.width = css_length::parse_length(decl.value);
    } else if (decl.property == "height") {
        style.height = css_length::parse_length(decl.value);
    } else if (decl.property == "min-width") {
        style.min_width = css_length::parse_length(decl.value);
    } else if (decl.property == "max-width") {
        style.max_width = css_length::parse_length(decl.value);
    } else if (decl.property == "min-height") {
        style.min_height = css_length::parse_length(decl.value);
    } else if (decl.property == "justify-content") {
        style.justify = parse_align(decl.value);
    } else if (decl.property == "align-items") {
        style.align_items = parse_align(decl.value);
    } else if (decl.property == "text-align") {
        style.text_align = parse_text_align(decl.value);
    } else if (decl.property == "white-space") {
        style.white_space = parse_white_space(decl.value);
    } else if (decl.property == "border-radius") {
        if (const auto radius = css_length::parse_length(decl.value)) {
            style.border_radius = *radius;
        }
    } else if (decl.property == "border-width") {
        if (const auto width = css_length::parse_length(decl.value)) {
            style.border_width = *width;
        }
    } else if (decl.property == "border-color") {
        if (const auto color = parse_color(decl.value)) {
            style.border_color = *color;
        }
    } else if (decl.property == "x1") {
        if (const auto length = css_length::parse_length(decl.value)) {
            style.x1 = *length;
        }
    } else if (decl.property == "y1") {
        if (const auto length = css_length::parse_length(decl.value)) {
            style.y1 = *length;
        }
    } else if (decl.property == "x2") {
        if (const auto length = css_length::parse_length(decl.value)) {
            style.x2 = *length;
        }
    } else if (decl.property == "y2") {
        if (const auto length = css_length::parse_length(decl.value)) {
            style.y2 = *length;
        }
    } else if (decl.property == "stroke") {
        if (const auto color = parse_color(decl.value)) {
            style.stroke = *color;
        }
    } else if (decl.property == "stroke-width") {
        if (const auto width = css_length::parse_length(decl.value)) {
            style.stroke_width = *width;
        }
    } else if (decl.property == "font-size") {
        if (const auto size = css_length::parse_length(decl.value)) {
            style.font_size = *size;
        }
    } else if (decl.property == "line-height") {
        if (const auto line_height = parse_line_height(decl.value)) {
            style.line_height = *line_height;
        }
    } else if (decl.property == "font-family") {
        const std::string_view value = trim(decl.value);
        if (value == "default" || value.empty()) {
            style.font_family = builtin::font_ui;
        } else if (const auto id = AssetId::parse(value)) {
            style.font_family = *id;
        }
    } else if (decl.property == "animation-name") {
        style.animation_name = std::string(trim(decl.value));
    } else if (decl.property == "animation-duration") {
        if (const auto duration = parse_seconds(decl.value)) {
            style.animation_duration = *duration;
        }
    } else if (decl.property == "z-index") {
        style.z_index = static_cast<int>(std::strtol(decl.value.c_str(), nullptr, 10));
    } else if (decl.property == "position") {
        style.position = parse_position(decl.value);
    } else if (decl.property == "top") {
        style.inset_top = css_length::parse_length(decl.value);
    } else if (decl.property == "right") {
        style.inset_right = css_length::parse_length(decl.value);
    } else if (decl.property == "bottom") {
        style.inset_bottom = css_length::parse_length(decl.value);
    } else if (decl.property == "left") {
        style.inset_left = css_length::parse_length(decl.value);
    } else if (decl.property == "transform") {
        const ParsedTransform transform = parse_transform(decl.value);
        style.rotation_deg = transform.rotation_deg;
        style.scale = transform.scale;
    } else if (decl.property == "overflow") {
        auto parse_overflow = [](std::string_view val) -> Overflow {
            const std::string_view t = trim(val);
            if (t == "hidden") return Overflow::Hidden;
            if (t == "scroll") return Overflow::Scroll;
            if (t == "auto") return Overflow::Auto;
            return Overflow::Visible;
        };
        style.overflow_x = parse_overflow(decl.value);
        style.overflow_y = style.overflow_x;
        style.has_overflow_x = true;
        style.has_overflow_y = true;
    } else if (decl.property == "overflow-x") {
        auto parse_overflow = [](std::string_view val) -> Overflow {
            const std::string_view t = trim(val);
            if (t == "hidden") return Overflow::Hidden;
            if (t == "scroll") return Overflow::Scroll;
            if (t == "auto") return Overflow::Auto;
            return Overflow::Visible;
        };
        style.overflow_x = parse_overflow(decl.value);
        style.has_overflow_x = true;
    } else if (decl.property == "overflow-y") {
        auto parse_overflow = [](std::string_view val) -> Overflow {
            const std::string_view t = trim(val);
            if (t == "hidden") return Overflow::Hidden;
            if (t == "scroll") return Overflow::Scroll;
            if (t == "auto") return Overflow::Auto;
            return Overflow::Visible;
        };
        style.overflow_y = parse_overflow(decl.value);
        style.has_overflow_y = true;
    } else if (decl.property == "scrollbar-width") {
        const std::string_view val = trim(decl.value);
        style.has_scrollbar_width = true;
        if (val == "none") {
            style.scrollbar_width = Length{0.0f, LengthUnit::Px};
        } else if (val == "thin") {
            style.scrollbar_width = Length{4.0f, LengthUnit::Px};
        } else if (val == "auto") {
            style.scrollbar_width = Length{8.0f, LengthUnit::Px};
        } else if (const auto len = css_length::parse_length(decl.value)) {
            style.scrollbar_width = *len;
        }
    } else if (decl.property == "scrollbar-color") {
        const std::string_view val = trim(decl.value);
        if (val != "auto") {
            const auto space = val.find(' ');
            if (space != std::string_view::npos) {
                if (const auto thumb = parse_color(val.substr(0, space))) {
                    style.scrollbar_thumb_color = *thumb;
                    style.has_scrollbar_thumb_color = true;
                }
                if (const auto track = parse_color(val.substr(space + 1))) {
                    style.scrollbar_track_color = *track;
                    style.has_scrollbar_track_color = true;
                }
            }
        }
    } else if (decl.property == "scrollbar-thumb-color") {
        if (const auto col = parse_color(decl.value)) {
            style.scrollbar_thumb_color = *col;
            style.has_scrollbar_thumb_color = true;
        }
    } else if (decl.property == "scrollbar-track-color") {
        if (const auto col = parse_color(decl.value)) {
            style.scrollbar_track_color = *col;
            style.has_scrollbar_track_color = true;
        }
    } else if (decl.property == "scrollbar-thumb-hover-color") {
        if (const auto col = parse_color(decl.value)) {
            style.scrollbar_thumb_hover_color = *col;
            style.has_scrollbar_thumb_hover_color = true;
        }
    } else if (decl.property == "scrollbar-border-radius") {
        if (const auto radius = css_length::parse_length(decl.value)) {
            style.scrollbar_border_radius = *radius;
            style.has_scrollbar_border_radius = true;
        }
    } else if (decl.property == "selection-color") {
        if (const auto col = parse_color(decl.value)) {
            style.selection_color = *col;
            style.has_selection_color = true;
        }
    }
}

struct VarReference {
    std::string_view name;  // without the leading "--"
    std::optional<std::string_view> fallback;
};

// Parses `var(--name)` / `var(--name, fallback)`. Anything else (a literal value, or a value that
// merely contains "var(" as text) returns nullopt and is left for apply_declaration as-is.
std::optional<VarReference> parse_var_reference(std::string_view value) {
    const std::string_view trimmed = trim(value);
    constexpr std::string_view kFuncPrefix = "var(";
    if (!trimmed.starts_with(kFuncPrefix) || !trimmed.ends_with(')')) {
        return std::nullopt;
    }
    const std::string_view inner = trim(trimmed.substr(kFuncPrefix.size(), trimmed.size() - kFuncPrefix.size() - 1));
    const auto comma = inner.find(',');
    const std::string_view raw_name = trim(comma == std::string_view::npos ? inner : inner.substr(0, comma));
    constexpr std::string_view kVarPrefix = "--";
    if (!raw_name.starts_with(kVarPrefix)) {
        return std::nullopt;
    }
    VarReference ref;
    ref.name = raw_name.substr(kVarPrefix.size());
    if (comma != std::string_view::npos) {
        ref.fallback = trim(inner.substr(comma + 1));
    }
    return ref;
}

// Element::custom_properties (per-instance, refreshed every frame from a `var-<name>="{binding}"`
// XML attribute in bind_element) takes priority over ComputedStyle::custom_properties (cascaded
// `--name: value;` stylesheet declarations), matching how an inline override would beat a class
// rule. Unresolved with no fallback resolves to an empty value — the same graceful no-op every
// other apply_declaration parser already falls back to on invalid input.
CssDeclaration resolve_var(const CssDeclaration& decl, const Element& element, const ComputedStyle& style) {
    const auto ref = parse_var_reference(decl.value);
    if (!ref) {
        return decl;
    }
    const std::string name(ref->name);
    if (const auto it = element.custom_properties.find(name); it != element.custom_properties.end()) {
        return CssDeclaration{decl.property, it->second};
    }
    if (const auto it = style.custom_properties.find(name); it != style.custom_properties.end()) {
        return CssDeclaration{decl.property, it->second};
    }
    if (ref->fallback) {
        return CssDeclaration{decl.property, std::string(*ref->fallback)};
    }
    return CssDeclaration{decl.property, std::string{}};
}

bool media_matches(const std::optional<MediaQuery>& media, float window_width, float window_height) {
    if (!media) {
        return true;
    }
    if (media->feature == MediaFeature::MinWidth) {
        return window_width >= media->px;
    }
    return window_height >= media->px;
}

const Keyframes* find_keyframes(const Stylesheet& sheet, std::string_view name) {
    for (const Keyframes& keyframes : sheet.keyframes) {
        if (keyframes.name == name) {
            return &keyframes;
        }
    }
    return nullptr;
}

std::optional<float> sample_opacity(const Keyframes& keyframes, float t) {
    struct Stop {
        float offset = 0.0f;
        float opacity = 1.0f;
    };
    std::vector<Stop> stops;
    for (const KeyframeStop& stop : keyframes.stops) {
        for (const CssDeclaration& decl : stop.declarations) {
            if (decl.property == "opacity") {
                stops.push_back(Stop{stop.offset, std::strtof(decl.value.c_str(), nullptr)});
                break;
            }
        }
    }
    if (stops.empty()) {
        return std::nullopt;
    }
    std::sort(stops.begin(), stops.end(), [](const Stop& a, const Stop& b) { return a.offset < b.offset; });
    t = std::clamp(t, 0.0f, 1.0f);
    if (t <= stops.front().offset) {
        return stops.front().opacity;
    }
    if (t >= stops.back().offset) {
        return stops.back().opacity;
    }
    for (std::size_t i = 0; i + 1 < stops.size(); ++i) {
        if (t > stops[i + 1].offset) {
            continue;
        }
        const float span = stops[i + 1].offset - stops[i].offset;
        const float u = span > 0.0f ? (t - stops[i].offset) / span : 0.0f;
        return stops[i].opacity + (stops[i + 1].opacity - stops[i].opacity) * u;
    }
    return stops.back().opacity;
}

void apply_animation_opacity(Element& element, ComputedStyle& style, const Stylesheet* sheet, float delta_time) {
    if (sheet == nullptr || style.animation_name.empty()) {
        return;
    }
    const Keyframes* keyframes = find_keyframes(*sheet, style.animation_name);
    if (keyframes == nullptr) {
        return;
    }
    element.animation_elapsed += delta_time;
    float t = 0.0f;
    if (style.animation_duration > 0.0f) {
        if (element.animation_elapsed > style.animation_duration) {
            element.animation_elapsed = style.animation_duration;
        }
        t = element.animation_elapsed / style.animation_duration;
    }
    if (const auto opacity = sample_opacity(*keyframes, t)) {
        style.opacity = *opacity;
    }
}

ComputedStyle compute_style_uncached(const Element& element, const Stylesheet* sheet, bool allow_pseudo,
        const std::vector<const Element*>& ancestors, float window_width, float window_height) {
    ComputedStyle style;
    if (sheet == nullptr) {
        return style;
    }

    struct Ranked {
        int spec = 0;
        std::size_t index = 0;
        const CssRule* rule = nullptr;
    };
    std::vector<Ranked> matched;
    for (std::size_t i = 0; i < sheet->rules.size(); ++i) {
        const CssRule& rule = sheet->rules[i];
        if (!media_matches(rule.media, window_width, window_height)) {
            continue;
        }
        if (!selector_matches(rule, element, ancestors, allow_pseudo)) {
            continue;
        }
        matched.push_back(Ranked{specificity(rule), i, &rule});
    }
    std::stable_sort(matched.begin(), matched.end(), [](const Ranked& a, const Ranked& b) {
        if (a.spec != b.spec) {
            return a.spec < b.spec;
        }
        return a.index < b.index;
    });
    for (const Ranked& item : matched) {
        for (const CssDeclaration& decl : item.rule->declarations) {
            if (decl.property.starts_with("--")) {
                style.custom_properties[decl.property.substr(2)] = decl.value;
                continue;
            }
            apply_declaration(style, resolve_var(decl, element, style));
        }
    }
    return style;
}

// Packs one element's pseudo-state into a byte so StyleCacheEntry::ancestor_pseudo_state can
// detect "A:hover B" reacting to ancestor A's hover changing, even though the ancestor pointer
// chain to B (identity, checked separately) didn't change.
std::uint8_t pseudo_state_bits(const Element& element) {
    return static_cast<std::uint8_t>((element.hovered ? 1u : 0u) | (element.pressed ? 2u : 0u) |
            (element.disabled ? 4u : 0u) | (element.focused ? 8u : 0u) | (element.checked ? 16u : 0u));
}

std::vector<std::uint8_t> ancestor_pseudo_state_bits(const std::vector<const Element*>& ancestors) {
    std::vector<std::uint8_t> bits;
    bits.reserve(ancestors.size());
    for (const Element* ancestor : ancestors) {
        bits.push_back(pseudo_state_bits(*ancestor));
    }
    return bits;
}

bool style_cache_hits(const StyleCacheEntry& cache, const Element& element, const Stylesheet* sheet,
        const std::vector<const Element*>& ancestors, float window_width, float window_height) {
    const std::uint64_t sheet_generation = sheet != nullptr ? sheet->generation : 0;
    if (!cache.valid || cache.sheet != sheet || cache.sheet_generation != sheet_generation ||
            cache.window_width != window_width || cache.window_height != window_height) {
        return false;
    }
    if (cache.id != element.id || cache.classes != element.classes) {
        return false;
    }
    if (cache.hovered != element.hovered || cache.pressed != element.pressed ||
            cache.disabled != element.disabled || cache.focused != element.focused ||
            cache.checked != element.checked) {
        return false;
    }
    if (cache.custom_properties != element.custom_properties) {
        return false;
    }
    if (cache.ancestors != ancestors) {
        return false;
    }
    return cache.ancestor_pseudo_state == ancestor_pseudo_state_bits(ancestors);
}

void style_cache_store(StyleCacheEntry& cache, const Element& element, const Stylesheet* sheet,
        const std::vector<const Element*>& ancestors, float window_width, float window_height, ComputedStyle style) {
    cache.valid = true;
    cache.sheet = sheet;
    cache.sheet_generation = sheet != nullptr ? sheet->generation : 0;
    cache.window_width = window_width;
    cache.window_height = window_height;
    cache.id = element.id;
    cache.classes = element.classes;
    cache.hovered = element.hovered;
    cache.pressed = element.pressed;
    cache.disabled = element.disabled;
    cache.focused = element.focused;
    cache.checked = element.checked;
    cache.custom_properties = element.custom_properties;
    cache.ancestors = ancestors;
    cache.ancestor_pseudo_state = ancestor_pseudo_state_bits(ancestors);
    cache.style = std::move(style);
}

// Memoized compute_style(): apply_layout_style() and paint_element() are the only two callers,
// with allow_pseudo false and true respectively, so they land in independent StyleCacheEntry
// slots on the element (style_cache_layout_ / style_cache_paint_) and can never invalidate each
// other. See StyleCacheEntry's doc comment (document.h) for exactly what a hit requires.
//
// Not cached at all: apply_animation_opacity(). It runs on the returned-by-value ComputedStyle in
// paint_element() every call, hit or miss, so keyframe opacity still advances every frame even
// when every other style input is unchanged.
ComputedStyle compute_style(const Element& element, const Stylesheet* sheet, bool allow_pseudo,
        const std::vector<const Element*>& ancestors, float window_width, float window_height) {
    StyleCacheEntry& cache = allow_pseudo ? element.style_cache_paint_ : element.style_cache_layout_;
    if (style_cache_hits(cache, element, sheet, ancestors, window_width, window_height)) {
        return cache.style;
    }
    ComputedStyle style = compute_style_uncached(element, sheet, allow_pseudo, ancestors, window_width, window_height);
    style_cache_store(cache, element, sheet, ancestors, window_width, window_height, style);
    return style;
}

BoxInsets resolve_insets(const LengthInsets& insets, glm::vec2 parent_content, float em_basis) {
    return BoxInsets{
            resolve_length(insets.top, parent_content.y, em_basis),
            resolve_length(insets.right, parent_content.x, em_basis),
            resolve_length(insets.bottom, parent_content.y, em_basis),
            resolve_length(insets.left, parent_content.x, em_basis),
    };
}

void apply_layout_style(Element& element, const Stylesheet* sheet, std::vector<const Element*>& ancestors,
        float window_width, float window_height) {
    if (element.kind == ElementKind::ItemTemplate) {
        return;
    }
    if (element.is_virtualization_spacer) {
        // Height is a pure function of skipped-row count * row stride, computed once in
        // bind_element (document.cpp, ItemsControl virtualization) — the stylesheet has no
        // opinion on it (the spacer deliberately carries no class/id, so no rule can match it
        // anyway), so leave every layout-affecting field exactly as bind_element set it instead of
        // resolving compute_style() and unconditionally overwriting `height` with (typically)
        // nullopt below. paint_element() still runs compute_style() on it normally for painting
        // (a separate cache slot) — with no matching rule that still resolves to a fully
        // transparent background, so nothing is drawn.
        return;
    }
    const ComputedStyle style = compute_style(element, sheet, false, ancestors, window_width, window_height);
    element.visible = style.visible;
    element.display_none = style.display_none;
    if (style.display_none) {
        // Layout skips it from now on, so drop any geometry left from when it was displayed.
        element.layout_rect = render::Rect{};
    }
    if (style.has_gap) {
        element.gap = style.gap;
    }
    if (style.has_direction) {
        element.direction = style.direction;
    }
    element.padding = style.padding;
    element.margin = style.margin;
    element.width = style.width;
    element.height = style.height;
    element.min_width = style.min_width;
    element.max_width = style.max_width;
    element.min_height = style.min_height;
    element.justify = style.justify;
    element.align_items = style.align_items;
    element.text_align = style.text_align;
    element.white_space = style.white_space;
    element.font_size = style.font_size;
    element.line_height = style.line_height;
    element.font_family = style.font_family;
    element.z_index = style.z_index;
    element.position = style.position;
    element.inset_top = style.inset_top;
    element.inset_right = style.inset_right;
    element.inset_bottom = style.inset_bottom;
    element.inset_left = style.inset_left;
    element.rotation_deg = style.rotation_deg;
    element.scale = style.scale;
    if (style.has_overflow_x) {
        element.overflow_x = style.overflow_x;
    }
    if (style.has_overflow_y) {
        element.overflow_y = style.overflow_y;
    }
    if (style.has_scrollbar_width) {
        element.scrollbar_width = style.scrollbar_width;
    }
    if (style.has_scrollbar_track_color) {
        element.scrollbar_track_color = style.scrollbar_track_color;
    }
    if (style.has_scrollbar_thumb_color) {
        element.scrollbar_thumb_color = style.scrollbar_thumb_color;
    }
    if (style.has_scrollbar_thumb_hover_color) {
        element.scrollbar_thumb_hover_color = style.scrollbar_thumb_hover_color;
    }
    if (style.has_scrollbar_border_radius) {
        element.scrollbar_border_radius = style.scrollbar_border_radius;
    }
    if (style.has_selection_color) {
        element.selection_color = style.selection_color;
    }
    ancestors.push_back(&element);
    for (Element& child : element.children) {
        apply_layout_style(child, sheet, ancestors, window_width, window_height);
    }
    for (Element& child : element.generated_items) {
        apply_layout_style(child, sheet, ancestors, window_width, window_height);
    }
    ancestors.pop_back();
}

}

void apply_layout_style(Element& root, const Stylesheet* sheet, float window_width, float window_height) {
    std::vector<const Element*> ancestors;
    apply_layout_style(root, sheet, ancestors, window_width, window_height);
}

namespace {

void clear_interaction(Element& element) {
    element.hovered = false;
    element.pressed = false;
    if (element.kind == ElementKind::ItemTemplate) {
        return;
    }
    for (Element& child : element.children) {
        clear_interaction(child);
    }
    for (Element& child : element.generated_items) {
        clear_interaction(child);
    }
}

// Only the single topmost element under the pointer (the same stacking order paint and click
// resolution use, via the shared hit_test()) gets :hover/:pressed. Previously every
// geometrically-overlapping Button was hovered independently, which z-index/position/rotation
// turn from a rare accident into a common, intentional case.
void apply_interaction(Element& root, glm::vec2 pointer, bool pointer_down) {
    clear_interaction(root);
    Element* hit = hit_test(root, pointer.x, pointer.y);
    if (hit != nullptr && !hit->disabled) {
        hit->hovered = true;
        hit->pressed = pointer_down;
    }
}

void paint_element(Element& element, const Stylesheet* sheet, IUiPainter& painter,
        std::vector<const Element*>& ancestors, glm::vec2 parent_content, const UiPaintInput& input) {
    if (element.kind == ElementKind::ItemTemplate) {
        return;
    }

    ComputedStyle style =
            compute_style(element, sheet, true, ancestors, input.window_width, input.window_height);
    if (!style.visible || style.display_none) {
        return;
    }
    apply_animation_opacity(element, style, sheet, input.delta_time);

    const float font_size = resolve_font_size(style.font_size, parent_content.x);
    const BoxInsets padding = resolve_insets(style.padding, parent_content, font_size);
    const float border_radius = resolve_length(style.border_radius, parent_content.x, font_size);
    const float border_width = resolve_length(style.border_width, parent_content.x, font_size);

    const render::Rect screen_rect = scale_rect(element.layout_rect, input.ui_offset, input.ui_scale);
    const float screen_border_radius = border_radius * input.ui_scale;
    const float screen_border_width = border_width * input.ui_scale;

    painter.save();
    painter.set_opacity(style.opacity);
    if (style.rotation_deg != 0.0f || style.scale != 1.0f) {
        // Read from this call's own pseudo-aware `style` (allow_pseudo=true above), not
        // element.rotation_deg/element.scale — those are apply_layout_style's allow_pseudo=false
        // values, frozen at layout time. transform is purely a paint-time visual effect (it never
        // affects layout_rect, unlike width/height/padding/etc, which genuinely can't be
        // pseudo-reactive without re-running layout), so a `:hover`/`:pressed` rule that changes
        // `transform` has no reason to wait for the next layout pass — reading the stale field
        // here silently dropped that effect entirely.
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        const glm::vec2 center{screen_rect.x + screen_rect.w * 0.5f, screen_rect.y + screen_rect.h * 0.5f};
        painter.apply_transform(center, style.rotation_deg * kDegToRad, style.scale);
    }
    // Must come after apply_transform: nvgScissor() bakes in whatever transform is active when
    // called, so a scissor set before a rotation clips against the element's un-rotated
    // axis-aligned rect instead of rotating along with the content — a rotated thin/long element
    // (e.g. a strike-through line) then gets clipped down to little more than where its rotated
    // bounds cross that stale rect, rather than its full rotated length.
    painter.scissor(screen_rect);

    if (style.background.a > 0.0f) {
        painter.fill_rounded_rect(screen_rect, screen_border_radius, style.background);
    }
    if (style.background_gradient) {
        painter.fill_rounded_rect_gradient(screen_rect, screen_border_radius, *style.background_gradient);
    } else if (style.background_image) {
        if (style.background_slice) {
            const BoxInsets slice = resolve_insets(*style.background_slice, parent_content, font_size);
            const BoxInsets screen_slice{
                    slice.top * input.ui_scale,
                    slice.right * input.ui_scale,
                    slice.bottom * input.ui_scale,
                    slice.left * input.ui_scale,
            };
            painter.image_nine_slice(*style.background_image, screen_rect, screen_slice);
        } else if (style.background_repeat == BackgroundRepeat::Repeat) {
            painter.image_repeat(*style.background_image, screen_rect);
        } else {
            painter.image(*style.background_image, screen_rect);
        }
    }
    if (border_width > 0.0f && style.border_color.a > 0.0f) {
        painter.stroke_rounded_rect(screen_rect, screen_border_radius, screen_border_width, style.border_color);
    }
    if (element.kind == ElementKind::Line) {
        const float x1 = resolve_length(style.x1, parent_content.x, font_size);
        const float y1 = resolve_length(style.y1, parent_content.y, font_size);
        const float x2 = resolve_length(style.x2, parent_content.x, font_size);
        const float y2 = resolve_length(style.y2, parent_content.y, font_size);
        const float stroke_width = resolve_length(style.stroke_width, parent_content.x, font_size);
        const glm::vec2 from{screen_rect.x + x1 * input.ui_scale, screen_rect.y + y1 * input.ui_scale};
        const glm::vec2 to{screen_rect.x + x2 * input.ui_scale, screen_rect.y + y2 * input.ui_scale};
        painter.draw_line(from, to, style.stroke, stroke_width * input.ui_scale);
    }

    if (element.paint != nullptr) {
        const render::Rect content_local{
                0.0f,
                0.0f,
                std::max(0.0f, element.layout_rect.w - padding.left - padding.right),
                std::max(0.0f, element.layout_rect.h - padding.top - padding.bottom),
        };
        const glm::vec2 origin{
                screen_rect.x + padding.left * input.ui_scale,
                screen_rect.y + padding.top * input.ui_scale,
        };
        DrawListAdapter list(painter, origin, input.ui_scale);
        element.paint->paint(list, content_local);
    }

    if (element.kind == ElementKind::Math) {
        if (const math::MathFont* math_font = painter.math_font()) {
            const render::Rect content{
                    screen_rect.x + padding.left * input.ui_scale,
                    screen_rect.y + padding.top * input.ui_scale,
                    std::max(0.0f, screen_rect.w - (padding.left + padding.right) * input.ui_scale),
                    std::max(0.0f, screen_rect.h - (padding.top + padding.bottom) * input.ui_scale),
            };
            math::paint_element(painter, *math_font, element, font_size, content, input.ui_scale, style.text_align,
                    style.align_items, style.color);
        }
    }

    if (element.kind == ElementKind::Label || element.kind == ElementKind::Button ||
            element.kind == ElementKind::TextInput) {
        painter.set_font(style.font_family, font_size * input.ui_scale);
        const render::Rect content{
                screen_rect.x + padding.left * input.ui_scale,
                screen_rect.y + padding.top * input.ui_scale,
                std::max(0.0f, screen_rect.w - (padding.left + padding.right) * input.ui_scale),
                std::max(0.0f, screen_rect.h - (padding.top + padding.bottom) * input.ui_scale),
        };
        const float x = text_align_origin_x(content.x, content.w, style.text_align);
        float y = content.y;
        if (style.align_items == UiAlign::Center) {
            y = content.y + content.h * 0.5f;
        } else if (style.align_items == UiAlign::End) {
            y = content.y + content.h;
        }
        const TextBlock* rows = nullptr;
        if ((element.kind == ElementKind::Label || element.kind == ElementKind::Button) && !element.text.empty()) {
            const float content_width = std::max(0.0f, element.layout_rect.w - padding.left - padding.right);
            rows = wrapped_text_rows(element, painter, font_size, content_width);
        }
        if (rows != nullptr) {
            // One fill_text per row, top-aligned, stacked from where align-items puts the whole block. Rows are
            // the ones layout broke at design size, so a scaled font cannot re-break them differently.
            const float row_h = rows->line_height * input.ui_scale;
            const float block_h = row_h * static_cast<float>(rows->lines.size());
            float top = content.y;
            if (style.align_items == UiAlign::Center) {
                top = content.y + (content.h - block_h) * 0.5f;
            } else if (style.align_items == UiAlign::End) {
                top = content.y + content.h - block_h;
            }
            top = std::max(top, content.y);
            for (std::size_t i = 0; i < rows->lines.size(); ++i) {
                const TextLine& line = rows->lines[i];
                if (line.begin == line.end) {
                    continue;
                }
                painter.fill_text(std::string_view(element.text).substr(line.begin, line.end - line.begin),
                        glm::vec2{x, top + static_cast<float>(i) * row_h}, style.color, style.text_align,
                        UiAlign::Start);
            }
        } else if (!element.text.empty()) {
            painter.fill_text(element.text, glm::vec2{x, y}, style.color, style.text_align, style.align_items);
        }
        if (element.kind == ElementKind::TextInput) {
            // Cached regardless of focus: a click that *focuses* an unfocused TextInput still
            // needs last frame's metrics to place the caret (see Element::painted_font_size_px).
            element.painted_font_size_px = font_size * input.ui_scale;
            element.painted_content_origin_x = x;
        }
        if (element.kind == ElementKind::TextInput && element.focused) {
            float glyph_y = content.y;
            if (style.align_items == UiAlign::Center) {
                glyph_y = content.y + std::max(0.0f, content.h - font_size * input.ui_scale) * 0.5f;
            } else if (style.align_items == UiAlign::End) {
                glyph_y = content.y + std::max(0.0f, content.h - font_size * input.ui_scale);
            }
            const float glyph_h = font_size * input.ui_scale;

            // Highlight behind the text, drawn whenever a real (non-collapsed) selection exists —
            // unlike the caret below, not gated on the blink phase.
            if (element.selection_anchor && *element.selection_anchor != element.caret_position) {
                const std::size_t sel_start =
                        std::min(std::min(*element.selection_anchor, element.caret_position), element.text.size());
                const std::size_t sel_end =
                        std::min(std::max(*element.selection_anchor, element.caret_position), element.text.size());
                const float start_w =
                        painter.measure_text(std::string_view(element.text).substr(0, sel_start), style.font_family,
                                font_size * input.ui_scale)
                                .x;
                const float end_w =
                        painter.measure_text(std::string_view(element.text).substr(0, sel_end), style.font_family,
                                font_size * input.ui_scale)
                                .x;
                painter.fill_rounded_rect(
                        render::Rect{x + start_w, glyph_y, end_w - start_w, glyph_h}, 0.0f, element.selection_color);
            }

            element.caret_blink_timer += input.delta_time;
            if (std::fmod(element.caret_blink_timer, 1.0f) < 0.5f) {
                const std::size_t caret_pos = std::min(element.caret_position, element.text.size());
                const std::string_view prefix = std::string_view(element.text).substr(0, caret_pos);
                const float text_w = painter.measure_text(prefix, style.font_family, font_size * input.ui_scale).x;
                const float caret_x = x + text_w;
                painter.draw_line(glm::vec2{caret_x, glyph_y}, glm::vec2{caret_x, glyph_y + glyph_h}, style.color,
                        1.0f * input.ui_scale);
            }
        }
    }
    if (element.kind == ElementKind::Image && element.source) {
        if (!style.background_image || *element.source != *style.background_image) {
            const auto& slice_insets = element.slice ? element.slice : style.background_slice;
            if (slice_insets) {
                const BoxInsets slice = resolve_insets(*slice_insets, parent_content, font_size);
                const BoxInsets screen_slice{
                        slice.top * input.ui_scale,
                        slice.right * input.ui_scale,
                        slice.bottom * input.ui_scale,
                        slice.left * input.ui_scale,
                };
                painter.image_nine_slice(*element.source, screen_rect, screen_slice);
            } else {
                painter.image(*element.source, screen_rect);
            }
        }
    }

    const glm::vec2 child_content{
            std::max(0.0f, element.layout_rect.w - padding.left - padding.right),
            std::max(0.0f, element.layout_rect.h - padding.top - padding.bottom),
    };
    ancestors.push_back(&element);
    const bool viewport_camera = element.kind == ElementKind::Viewport;
    const bool has_scroll = (element.scroll_x != 0.0f || element.scroll_y != 0.0f);
    if (viewport_camera) {
        painter.save();
        const glm::vec2 origin{screen_rect.x, screen_rect.y};
        const glm::vec2 pan{element.pan_x * input.ui_scale, element.pan_y * input.ui_scale};
        painter.apply_view(origin, pan, viewport_zoom(element.zoom));
    } else if (has_scroll) {
        painter.save();
        const glm::vec2 origin{screen_rect.x, screen_rect.y};
        const glm::vec2 pan{-element.scroll_x * input.ui_scale, -element.scroll_y * input.ui_scale};
        painter.apply_view(origin, pan, 1.0f);
    }
    if (element.kind == ElementKind::ItemsControl) {
        for (Element* child : child_stacking_order(element.generated_items)) {
            paint_element(*child, sheet, painter, ancestors, child_content, input);
        }
    } else {
        for (Element* child : child_stacking_order(element.children)) {
            paint_element(*child, sheet, painter, ancestors, child_content, input);
        }
    }
    if (viewport_camera || has_scroll) {
        painter.restore();
    }
    ancestors.pop_back();

    if (is_scrollable_y(element)) {
        const render::Rect track = scrollbar_track_rect(element, input.ui_scale);
        const render::Rect thumb = scrollbar_thumb_rect(element, input.ui_scale);
        if (track.w > 0.0f && track.h > 0.0f) {
            const render::Rect screen_track = scale_rect(track, input.ui_offset, input.ui_scale);
            const render::Rect screen_thumb = scale_rect(thumb, input.ui_offset, input.ui_scale);
            const float radius =
                    resolve_length(element.scrollbar_border_radius, parent_content.x, font_size) * input.ui_scale;
            if (element.scrollbar_track_color.a > 0.0f) {
                painter.fill_rounded_rect(screen_track, radius, element.scrollbar_track_color);
            }
            if (element.scrollbar_thumb_color.a > 0.0f) {
                const glm::vec4 thumb_color = (element.scrollbar_thumb_hovered || element.scrollbar_dragging)
                        ? element.scrollbar_thumb_hover_color
                        : element.scrollbar_thumb_color;
                painter.fill_rounded_rect(screen_thumb, radius, thumb_color);
            }
        }
    }

    painter.restore();
}

}

void paint_document(UiDocument& document, const Stylesheet* stylesheet, IUiPainter& painter, const UiPaintInput& input) {
    // wind-129 layout dirty-gate. Same "call layout_state_changed() unconditionally, never as a
    // short-circuited `||` operand" rule as prepare_top_canvas (canvas.cpp) — see that call site
    // for why. run_bind (systems.cpp) already called apply_bindings() unconditionally earlier this
    // frame's Bind phase, so document.root's text/custom_properties/generated_items are already
    // this frame's values by the time paint_document runs.
    const bool per_element_changed = layout_state_changed(document.root);
    const std::uint64_t sheet_generation = stylesheet != nullptr ? stylesheet->generation : 0;
    const bool layout_dirty = per_element_changed || !document.layout_computed_once ||
            document.last_canvas_layout_rect != input.canvas_rect || document.last_media_width != input.window_width ||
            document.last_media_height != input.window_height || document.last_layout_sheet != stylesheet ||
            document.last_layout_sheet_generation != sheet_generation ||
            document.last_layout_painter != static_cast<const void*>(&painter) ||
            document.last_layout_math_font != math_font_identity(&painter);
    if (layout_dirty) {
        apply_layout_style(document.root, stylesheet, input.window_width, input.window_height);
        layout(document, input.canvas_rect, &painter);
        document.layout_computed_once = true;
        document.last_canvas_layout_rect = input.canvas_rect;
        document.last_media_width = input.window_width;
        document.last_media_height = input.window_height;
        document.last_layout_sheet = stylesheet;
        document.last_layout_painter = &painter;
        document.last_layout_math_font = math_font_identity(&painter);
        document.last_layout_sheet_generation = sheet_generation;
    }
    apply_interaction(document.root, input.pointer, input.pointer_down);

    painter.save();
    painter.scissor(scale_rect(input.canvas_rect, input.ui_offset, input.ui_scale));
    std::vector<const Element*> ancestors;
    paint_element(document.root, stylesheet, painter, ancestors, glm::vec2{input.canvas_rect.w, input.canvas_rect.h},
            input);
    painter.restore();
}

}
