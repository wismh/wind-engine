#include "painter.h"

#include <engine/builtin_ids.h>
#include <engine/ui/canvas.h>

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
    float opacity = 1.0f;
    bool visible = true;
    float gap = 0.0f;
    bool has_gap = false;
    StackDirection direction = StackDirection::Vertical;
    bool has_direction = false;
    BoxInsets padding{};
    UiAlign justify = UiAlign::Start;
    UiAlign align_items = UiAlign::Start;
    float border_radius = 0.0f;
    float border_width = 0.0f;
    glm::vec4 border_color{0.0f, 0.0f, 0.0f, 0.0f};
    float font_size = 16.0f;
    AssetId font_family = builtin::font_ui;
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

std::optional<BoxInsets> parse_padding(std::string_view raw) {
    const std::string_view value = trim(raw);
    if (value.empty()) {
        return std::nullopt;
    }
    float parts[4] = {};
    int count = 0;
    std::size_t i = 0;
    while (i < value.size() && count < 4) {
        while (i < value.size() && std::isspace(static_cast<unsigned char>(value[i])) != 0) {
            ++i;
        }
        if (i >= value.size()) {
            break;
        }
        char* end = nullptr;
        const float n = std::strtof(value.data() + i, &end);
        if (end == value.data() + i) {
            return std::nullopt;
        }
        parts[count++] = n;
        i = static_cast<std::size_t>(end - value.data());
    }
    BoxInsets padding;
    if (count == 1) {
        padding.top = padding.right = padding.bottom = padding.left = parts[0];
    } else if (count == 2) {
        padding.top = padding.bottom = parts[0];
        padding.right = padding.left = parts[1];
    } else if (count == 3) {
        padding.top = parts[0];
        padding.right = padding.left = parts[1];
        padding.bottom = parts[2];
    } else if (count == 4) {
        padding.top = parts[0];
        padding.right = parts[1];
        padding.bottom = parts[2];
        padding.left = parts[3];
    } else {
        return std::nullopt;
    }
    return padding;
}

bool selector_matches(const CssSelector& selector, const Element& element, bool allow_pseudo) {
    switch (selector.type) {
        case CssSelectorType::Element:
            if (kind_name(element.kind) != selector.element) {
                return false;
            }
            break;
        case CssSelectorType::Class:
            if (element.class_name != selector.class_name) {
                return false;
            }
            break;
        case CssSelectorType::Id:
            if (element.id != selector.id) {
                return false;
            }
            break;
        case CssSelectorType::ElementClass:
            if (kind_name(element.kind) != selector.element || element.class_name != selector.class_name) {
                return false;
            }
            break;
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

int specificity(const CssSelector& selector) {
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

void apply_declaration(ComputedStyle& style, const CssDeclaration& decl) {
    if (decl.property == "color") {
        if (const auto color = parse_color(decl.value)) {
            style.color = *color;
        }
    } else if (decl.property == "background") {
        if (const auto color = parse_color(decl.value)) {
            style.background = *color;
        }
    } else if (decl.property == "opacity") {
        style.opacity = std::strtof(decl.value.c_str(), nullptr);
    } else if (decl.property == "visibility") {
        style.visible = trim(decl.value) != "hidden";
    } else if (decl.property == "gap") {
        style.gap = std::strtof(decl.value.c_str(), nullptr);
        style.has_gap = true;
    } else if (decl.property == "flex-direction") {
        const std::string_view value = trim(decl.value);
        style.has_direction = true;
        if (value == "horizontal" || value == "row") {
            style.direction = StackDirection::Horizontal;
        } else {
            style.direction = StackDirection::Vertical;
        }
    } else if (decl.property == "padding") {
        if (const auto padding = parse_padding(decl.value)) {
            style.padding = *padding;
        }
    } else if (decl.property == "justify-content") {
        style.justify = parse_align(decl.value);
    } else if (decl.property == "align-items") {
        style.align_items = parse_align(decl.value);
    } else if (decl.property == "border-radius") {
        style.border_radius = std::strtof(decl.value.c_str(), nullptr);
    } else if (decl.property == "border-width") {
        style.border_width = std::strtof(decl.value.c_str(), nullptr);
    } else if (decl.property == "border-color") {
        if (const auto color = parse_color(decl.value)) {
            style.border_color = *color;
        }
    } else if (decl.property == "font-size") {
        style.font_size = std::strtof(decl.value.c_str(), nullptr);
    } else if (decl.property == "font-family") {
        const std::string_view value = trim(decl.value);
        if (value == "default" || value.empty()) {
            style.font_family = builtin::font_ui;
        } else if (const auto id = AssetId::parse(value)) {
            style.font_family = *id;
        }
    }
}

ComputedStyle compute_style(const Element& element, const Stylesheet* sheet, bool allow_pseudo) {
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
        if (!selector_matches(rule.selector, element, allow_pseudo)) {
            continue;
        }
        matched.push_back(Ranked{specificity(rule.selector), i, &rule});
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

}

void apply_layout_style(Element& element, const Stylesheet* sheet) {
    if (element.kind == ElementKind::ItemTemplate) {
        return;
    }
    const ComputedStyle style = compute_style(element, sheet, false);
    if (style.has_gap) {
        element.gap = style.gap;
    }
    if (style.has_direction) {
        element.direction = style.direction;
    }
    element.padding = style.padding;
    for (Element& child : element.children) {
        apply_layout_style(child, sheet);
    }
    for (Element& child : element.generated_items) {
        apply_layout_style(child, sheet);
    }
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

void paint_element(Element& element, const Stylesheet* sheet, IUiPainter& painter) {
    if (element.kind == ElementKind::ItemTemplate) {
        return;
    }

    const ComputedStyle style = compute_style(element, sheet, true);
    if (!style.visible) {
        return;
    }

    painter.save();
    painter.set_opacity(style.opacity);
    painter.scissor(element.layout_rect);

    if (style.background.a > 0.0f) {
        painter.fill_rounded_rect(element.layout_rect, style.border_radius, style.background);
    }
    if (style.border_width > 0.0f && style.border_color.a > 0.0f) {
        painter.stroke_rounded_rect(element.layout_rect, style.border_radius, style.border_width, style.border_color);
    }

    if (element.kind == ElementKind::Label || element.kind == ElementKind::Button) {
        if (!element.text.empty()) {
            painter.set_font(style.font_family, style.font_size);
            const render::Rect content{
                    element.layout_rect.x + style.padding.left,
                    element.layout_rect.y + style.padding.top,
                    std::max(0.0f, element.layout_rect.w - style.padding.left - style.padding.right),
                    std::max(0.0f, element.layout_rect.h - style.padding.top - style.padding.bottom),
            };
            float x = content.x;
            if (style.justify == UiAlign::Center) {
                x = content.x + content.w * 0.5f;
            } else if (style.justify == UiAlign::End) {
                x = content.x + content.w;
            }
            float y = content.y;
            if (style.align_items == UiAlign::Center) {
                y = content.y + content.h * 0.5f;
            } else if (style.align_items == UiAlign::End) {
                y = content.y + content.h;
            }
            painter.fill_text(element.text, glm::vec2{x, y}, style.color, style.justify, style.align_items);
        }
    }
    if (element.kind == ElementKind::Image && element.source) {
        painter.image(*element.source, element.layout_rect);
    }

    if (element.kind == ElementKind::ItemsControl) {
        for (Element& child : element.generated_items) {
            paint_element(child, sheet, painter);
        }
    } else {
        for (Element& child : element.children) {
            paint_element(child, sheet, painter);
        }
    }

    painter.restore();
}

}

void paint_document(UiDocument& document, const Stylesheet* stylesheet, IUiPainter& painter, const UiPaintInput& input) {
    apply_layout_style(document.root, stylesheet);
    layout(document, input.canvas_rect);
    apply_interaction(document.root, input.pointer, input.pointer_down);

    painter.save();
    painter.scissor(input.canvas_rect);
    paint_element(document.root, stylesheet, painter);
    painter.restore();
}

}
