#include "svg_internal.h"

#include <algorithm>

namespace engine::ui::svg_detail {

std::optional<PaintColor> parse_color(std::string_view raw) {
    raw = trim(raw);
    const std::string key = lower_copy(raw);
    if (key == "none" || key == "transparent") {
        return PaintColor{ColorKind::None, {}};
    }
    if (key == "currentcolor") {
        return PaintColor{ColorKind::Current, {0.0f, 0.0f, 0.0f, 1.0f}};
    }
    if (key.starts_with("url(")) {
        return std::nullopt;
    }
    if (raw.empty() || raw.front() != '#') {
        return std::nullopt;
    }
    const std::string_view hex = raw.substr(1);
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return 10 + (c - 'a');
        }
        if (c >= 'A' && c <= 'F') {
            return 10 + (c - 'A');
        }
        return -1;
    };
    auto byte = [&](char hi, char lo) -> int {
        const int h = nibble(hi);
        const int l = nibble(lo);
        if (h < 0 || l < 0) {
            return -1;
        }
        return (h << 4) | l;
    };
    glm::vec4 color{0.0f, 0.0f, 0.0f, 1.0f};
    if (hex.size() == 3 || hex.size() == 4) {
        const int r = nibble(hex[0]);
        const int g = nibble(hex[1]);
        const int b = nibble(hex[2]);
        const int a = hex.size() == 4 ? nibble(hex[3]) : 15;
        if (r < 0 || g < 0 || b < 0 || a < 0) {
            return std::nullopt;
        }
        color = {r / 15.0f, g / 15.0f, b / 15.0f, a / 15.0f};
    } else if (hex.size() == 6 || hex.size() == 8) {
        const int r = byte(hex[0], hex[1]);
        const int g = byte(hex[2], hex[3]);
        const int b = byte(hex[4], hex[5]);
        const int a = hex.size() == 8 ? byte(hex[6], hex[7]) : 255;
        if (r < 0 || g < 0 || b < 0 || a < 0) {
            return std::nullopt;
        }
        color = {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    } else {
        return std::nullopt;
    }
    return PaintColor{ColorKind::Literal, color};
}

namespace {

bool dashed(std::string_view raw) {
    raw = trim(raw);
    const std::string key = lower_copy(raw);
    return !key.empty() && key != "none";
}

bool apply_decl(Style& style, std::string_view name, std::string_view value) {
    name = trim(name);
    if (name == "fill") {
        const auto color = parse_color(value);
        if (!color) {
            return false;
        }
        style.fill = *color;
        return true;
    }
    if (name == "stroke") {
        const auto color = parse_color(value);
        if (!color) {
            return false;
        }
        style.stroke = *color;
        return true;
    }
    if (name == "stroke-width") {
        const auto width = length_value(value);
        if (!width || *width < 0.0f) {
            return false;
        }
        style.stroke_width = *width;
        return true;
    }
    if (name == "opacity") {
        const auto amount = length_value(value);
        if (!amount) {
            return false;
        }
        style.opacity = std::clamp(*amount, 0.0f, 1.0f);
        return true;
    }
    if (name == "fill-opacity") {
        const auto amount = length_value(value);
        if (!amount) {
            return false;
        }
        style.fill_opacity = std::clamp(*amount, 0.0f, 1.0f);
        return true;
    }
    if (name == "stroke-opacity") {
        const auto amount = length_value(value);
        if (!amount) {
            return false;
        }
        style.stroke_opacity = std::clamp(*amount, 0.0f, 1.0f);
        return true;
    }
    if (name == "fill-rule") {
        return lower_copy(trim(value)) != "evenodd";
    }
    if (name == "stroke-dasharray") {
        return !dashed(value);
    }
    if (name == "display") {
        if (lower_copy(trim(value)) == "none") {
            style.skip = true;
        }
        return true;
    }
    if (name == "visibility") {
        if (lower_copy(trim(value)) == "hidden") {
            style.skip = true;
        }
        return true;
    }
    return true;
}

bool apply_style_attr(Style& style, std::string_view text) {
    while (!text.empty()) {
        const std::size_t semi = text.find(';');
        const std::string_view piece = trim(text.substr(0, semi));
        text = semi == std::string_view::npos ? std::string_view{} : text.substr(semi + 1);
        if (piece.empty()) {
            continue;
        }
        const std::size_t colon = piece.find(':');
        if (colon == std::string_view::npos) {
            return false;
        }
        if (!apply_decl(style, piece.substr(0, colon), piece.substr(colon + 1))) {
            return false;
        }
    }
    return true;
}

}

bool apply_presentation(Style& style, const tinyxml2::XMLElement* element) {
    // `opacity` is not inherited: the parent contributes a multiplier, and this element's own value replaces
    // only its local factor. The style attribute is applied after the attributes, so it wins.
    const float inherited_opacity = style.opacity;
    style.opacity = 1.0f;
    auto take = [&](const char* name) {
        if (const char* value = element->Attribute(name)) {
            return apply_decl(style, name, value);
        }
        return true;
    };
    if (!take("fill") || !take("stroke") || !take("stroke-width") || !take("opacity") || !take("fill-opacity") ||
        !take("stroke-opacity") || !take("fill-rule") || !take("stroke-dasharray") || !take("display") ||
        !take("visibility")) {
        return false;
    }
    if (const char* inline_style = element->Attribute("style")) {
        if (!apply_style_attr(style, inline_style)) {
            return false;
        }
    }
    style.opacity *= inherited_opacity;
    return true;
}

}
