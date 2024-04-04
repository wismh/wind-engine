#include "painter.h"
#include "css_length.h"

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
#include <utility>
#include <vector>

namespace engine::ui {
namespace {

struct ComputedStyle {
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 background{0.0f, 0.0f, 0.0f, 0.0f};
    std::optional<AssetId> background_image;
    float opacity = 1.0f;
    bool visible = true;
    Length gap{};
    bool has_gap = false;
    StackDirection direction = StackDirection::Vertical;
    bool has_direction = false;
    LengthInsets padding{};
    LengthInsets margin{};
    std::optional<Length> width;
    std::optional<Length> height;
    std::optional<Length> min_width;
    std::optional<Length> min_height;
    UiAlign justify = UiAlign::Start;
    UiAlign align_items = UiAlign::Start;
    UiAlign text_align = UiAlign::Start;
    Length border_radius{};
    Length border_width{};
    glm::vec4 border_color{0.0f, 0.0f, 0.0f, 0.0f};
    Length font_size{kDefaultFontSize, LengthUnit::Px};
    AssetId font_family = builtin::font_ui;
    std::string animation_name;
    float animation_duration = 0.0f;
};

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

UiAlign parse_align(std::string_view raw) {
    const std::string_view value = trim(raw);
    if (value == "center") {
        return UiAlign::Center;
    }
    if (value == "end" || value == "flex-end") {
        return UiAlign::End;
    }
    return UiAlign::Start;
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

bool compound_matches(const CssSelector& selector, const Element& element) {
    switch (selector.type) {
        case CssSelectorType::Element:
            return kind_name(element.kind) == selector.element;
        case CssSelectorType::Class:
            return element.class_name == selector.class_name;
        case CssSelectorType::Id:
            return element.id == selector.id;
        case CssSelectorType::ElementClass:
            return kind_name(element.kind) == selector.element && element.class_name == selector.class_name;
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
            if (!compound_matches(compound, *ancestors[pos])) {
                return false;
            }
        } else {
            bool found = false;
            while (pos > 0) {
                --pos;
                if (compound_matches(compound, *ancestors[pos])) {
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
        if (const auto color = parse_color(decl.value)) {
            style.background = *color;
        }
    } else if (decl.property == "background-image") {
        const std::string_view value = trim(decl.value);
        if (value == "none") {
            style.background_image.reset();
        } else if (const auto id = AssetId::parse(value)) {
            style.background_image = *id;
        }
    } else if (decl.property == "opacity") {
        style.opacity = std::strtof(decl.value.c_str(), nullptr);
    } else if (decl.property == "visibility") {
        style.visible = trim(decl.value) != "hidden";
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
    } else if (decl.property == "min-height") {
        style.min_height = css_length::parse_length(decl.value);
    } else if (decl.property == "justify-content") {
        style.justify = parse_align(decl.value);
    } else if (decl.property == "align-items") {
        style.align_items = parse_align(decl.value);
    } else if (decl.property == "text-align") {
        style.text_align = parse_text_align(decl.value);
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
    } else if (decl.property == "font-size") {
        if (const auto size = css_length::parse_length(decl.value)) {
            style.font_size = *size;
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
    }
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

ComputedStyle compute_style(const Element& element, const Stylesheet* sheet, bool allow_pseudo,
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
            apply_declaration(style, decl);
        }
    }
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
    const ComputedStyle style = compute_style(element, sheet, false, ancestors, window_width, window_height);
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
    element.min_height = style.min_height;
    element.justify = style.justify;
    element.align_items = style.align_items;
    element.text_align = style.text_align;
    element.font_size = style.font_size;
    element.font_family = style.font_family;
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

void apply_interaction(Element& element, glm::vec2 pointer, bool pointer_down) {
    if (element.kind == ElementKind::ItemTemplate) {
        element.hovered = false;
        element.pressed = false;
        return;
    }
    const bool inside = rect_contains(element.layout_rect, pointer.x, pointer.y);
    element.hovered = element.kind == ElementKind::Button && inside && !element.disabled;
    element.pressed = element.hovered && pointer_down;
    for (Element& child : element.children) {
        apply_interaction(child, pointer, pointer_down);
    }
    for (Element& child : element.generated_items) {
        apply_interaction(child, pointer, pointer_down);
    }
}

void paint_element(Element& element, const Stylesheet* sheet, IUiPainter& painter,
        std::vector<const Element*>& ancestors, glm::vec2 parent_content, const UiPaintInput& input) {
    if (element.kind == ElementKind::ItemTemplate) {
        return;
    }

    ComputedStyle style =
            compute_style(element, sheet, true, ancestors, input.window_width, input.window_height);
    if (!style.visible) {
        return;
    }
    apply_animation_opacity(element, style, sheet, input.delta_time);

    const float font_size = resolve_font_size(style.font_size, parent_content.x);
    const BoxInsets padding = resolve_insets(style.padding, parent_content, font_size);
    const float border_radius = resolve_length(style.border_radius, parent_content.x, font_size);
    const float border_width = resolve_length(style.border_width, parent_content.x, font_size);

    painter.save();
    painter.set_opacity(style.opacity);
    painter.scissor(element.layout_rect);

    if (style.background.a > 0.0f) {
        painter.fill_rounded_rect(element.layout_rect, border_radius, style.background);
    }
    if (style.background_image) {
        painter.image(*style.background_image, element.layout_rect);
    }
    if (border_width > 0.0f && style.border_color.a > 0.0f) {
        painter.stroke_rounded_rect(element.layout_rect, border_radius, border_width, style.border_color);
    }

    if (element.kind == ElementKind::Label || element.kind == ElementKind::Button) {
        if (!element.text.empty()) {
            painter.set_font(style.font_family, font_size);
            const render::Rect content{
                    element.layout_rect.x + padding.left,
                    element.layout_rect.y + padding.top,
                    std::max(0.0f, element.layout_rect.w - padding.left - padding.right),
                    std::max(0.0f, element.layout_rect.h - padding.top - padding.bottom),
            };
            float x = content.x;
            if (style.text_align == UiAlign::Center) {
                x = content.x + content.w * 0.5f;
            } else if (style.text_align == UiAlign::End) {
                x = content.x + content.w;
            }
            float y = content.y;
            if (style.align_items == UiAlign::Center) {
                y = content.y + content.h * 0.5f;
            } else if (style.align_items == UiAlign::End) {
                y = content.y + content.h;
            }
            painter.fill_text(element.text, glm::vec2{x, y}, style.color, style.text_align, style.align_items);
        }
    }
    if (element.kind == ElementKind::Image && element.source) {
        if (!style.background_image || *element.source != *style.background_image) {
            painter.image(*element.source, element.layout_rect);
        }
    }

    const glm::vec2 child_content{
            std::max(0.0f, element.layout_rect.w - padding.left - padding.right),
            std::max(0.0f, element.layout_rect.h - padding.top - padding.bottom),
    };
    ancestors.push_back(&element);
    if (element.kind == ElementKind::ItemsControl) {
        for (Element& child : element.generated_items) {
            paint_element(child, sheet, painter, ancestors, child_content, input);
        }
    } else {
        for (Element& child : element.children) {
            paint_element(child, sheet, painter, ancestors, child_content, input);
        }
    }
    ancestors.pop_back();

    painter.restore();
}

}

void paint_document(UiDocument& document, const Stylesheet* stylesheet, IUiPainter& painter, const UiPaintInput& input) {
    apply_layout_style(document.root, stylesheet, input.window_width, input.window_height);
    layout(document, input.canvas_rect, &painter);
    apply_interaction(document.root, input.pointer, input.pointer_down);

    painter.save();
    painter.scissor(input.canvas_rect);
    std::vector<const Element*> ancestors;
    paint_element(document.root, stylesheet, painter, ancestors, glm::vec2{input.canvas_rect.w, input.canvas_rect.h},
            input);
    painter.restore();
}

}
