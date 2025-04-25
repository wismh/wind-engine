#include "style_anim.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace engine::ui {
namespace {

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

std::string lower_copy(std::string_view value) {
    std::string out(value);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

template <typename T>
T list_last(const std::vector<T>& values, std::size_t index, T fallback) {
    if (values.empty()) {
        return fallback;
    }
    return index < values.size() ? values[index] : values.back();
}

constexpr MotionProp kAllProps[] = {
        MotionProp::Color,
        MotionProp::Background,
        MotionProp::Opacity,
        MotionProp::Visibility,
        MotionProp::Display,
        MotionProp::Width,
        MotionProp::Height,
        MotionProp::MinWidth,
        MotionProp::MaxWidth,
        MotionProp::MinHeight,
        MotionProp::Padding,
        MotionProp::Margin,
        MotionProp::Gap,
        MotionProp::FlexDirection,
        MotionProp::AlignItems,
        MotionProp::JustifyContent,
        MotionProp::TextAlign,
        MotionProp::WhiteSpace,
        MotionProp::UserSelect,
        MotionProp::BackgroundImage,
        MotionProp::BackgroundSlice,
        MotionProp::BackgroundRepeat,
        MotionProp::BorderRadius,
        MotionProp::BorderWidth,
        MotionProp::BorderColor,
        MotionProp::FontSize,
        MotionProp::LineHeight,
        MotionProp::FontFamily,
        MotionProp::ZIndex,
        MotionProp::Position,
        MotionProp::Top,
        MotionProp::Right,
        MotionProp::Bottom,
        MotionProp::Left,
        MotionProp::Transform,
        MotionProp::X1,
        MotionProp::Y1,
        MotionProp::X2,
        MotionProp::Y2,
        MotionProp::Stroke,
        MotionProp::StrokeWidth,
        MotionProp::OverflowX,
        MotionProp::OverflowY,
        MotionProp::ScrollbarWidth,
        MotionProp::ScrollbarThumbColor,
        MotionProp::ScrollbarTrackColor,
        MotionProp::ScrollbarThumbHoverColor,
        MotionProp::ScrollbarBorderRadius,
        MotionProp::SelectionColor,
};

struct NameMap {
    std::string_view name;
    MotionProp prop;
};

constexpr NameMap kNames[] = {
        {"color", MotionProp::Color},
        {"background", MotionProp::Background},
        {"opacity", MotionProp::Opacity},
        {"visibility", MotionProp::Visibility},
        {"display", MotionProp::Display},
        {"width", MotionProp::Width},
        {"height", MotionProp::Height},
        {"min-width", MotionProp::MinWidth},
        {"max-width", MotionProp::MaxWidth},
        {"min-height", MotionProp::MinHeight},
        {"padding", MotionProp::Padding},
        {"margin", MotionProp::Margin},
        {"gap", MotionProp::Gap},
        {"flex-direction", MotionProp::FlexDirection},
        {"align-items", MotionProp::AlignItems},
        {"justify-content", MotionProp::JustifyContent},
        {"text-align", MotionProp::TextAlign},
        {"white-space", MotionProp::WhiteSpace},
        {"user-select", MotionProp::UserSelect},
        {"background-image", MotionProp::BackgroundImage},
        {"background-slice", MotionProp::BackgroundSlice},
        {"background-repeat", MotionProp::BackgroundRepeat},
        {"border-radius", MotionProp::BorderRadius},
        {"border-width", MotionProp::BorderWidth},
        {"border-color", MotionProp::BorderColor},
        {"font-size", MotionProp::FontSize},
        {"line-height", MotionProp::LineHeight},
        {"font-family", MotionProp::FontFamily},
        {"z-index", MotionProp::ZIndex},
        {"position", MotionProp::Position},
        {"top", MotionProp::Top},
        {"right", MotionProp::Right},
        {"bottom", MotionProp::Bottom},
        {"left", MotionProp::Left},
        {"transform", MotionProp::Transform},
        {"x1", MotionProp::X1},
        {"y1", MotionProp::Y1},
        {"x2", MotionProp::X2},
        {"y2", MotionProp::Y2},
        {"stroke", MotionProp::Stroke},
        {"stroke-width", MotionProp::StrokeWidth},
        {"overflow", MotionProp::Overflow},
        {"overflow-x", MotionProp::OverflowX},
        {"overflow-y", MotionProp::OverflowY},
        {"scrollbar-width", MotionProp::ScrollbarWidth},
        {"scrollbar-color", MotionProp::ScrollbarColor},
        {"scrollbar-thumb-color", MotionProp::ScrollbarThumbColor},
        {"scrollbar-track-color", MotionProp::ScrollbarTrackColor},
        {"scrollbar-thumb-hover-color", MotionProp::ScrollbarThumbHoverColor},
        {"scrollbar-border-radius", MotionProp::ScrollbarBorderRadius},
        {"selection-color", MotionProp::SelectionColor},
};

std::optional<MotionProp> prop_from_name(std::string_view name) {
    for (const NameMap& entry : kNames) {
        if (entry.name == name) {
            return entry.prop;
        }
    }
    return std::nullopt;
}

bool decl_sets(std::string_view property, MotionProp prop) {
    const std::optional<MotionProp> named = prop_from_name(property);
    if (!named) {
        return false;
    }
    if (*named == prop) {
        return true;
    }
    if (*named == MotionProp::Overflow && (prop == MotionProp::OverflowX || prop == MotionProp::OverflowY)) {
        return true;
    }
    if (*named == MotionProp::ScrollbarColor &&
            (prop == MotionProp::ScrollbarThumbColor || prop == MotionProp::ScrollbarTrackColor)) {
        return true;
    }
    return false;
}

bool affects_layout(MotionProp prop) {
    switch (prop) {
        case MotionProp::Display:
        case MotionProp::Width:
        case MotionProp::Height:
        case MotionProp::MinWidth:
        case MotionProp::MaxWidth:
        case MotionProp::MinHeight:
        case MotionProp::Padding:
        case MotionProp::Margin:
        case MotionProp::Gap:
        case MotionProp::FlexDirection:
        case MotionProp::AlignItems:
        case MotionProp::JustifyContent:
        case MotionProp::WhiteSpace:
        case MotionProp::FontSize:
        case MotionProp::LineHeight:
        case MotionProp::FontFamily:
        case MotionProp::Position:
        case MotionProp::Top:
        case MotionProp::Right:
        case MotionProp::Bottom:
        case MotionProp::Left:
            return true;
        default:
            return false;
    }
}

bool affects_row_box(MotionProp prop) {
    switch (prop) {
        case MotionProp::Display:
        case MotionProp::Height:
        case MotionProp::MinHeight:
        case MotionProp::Padding:
        case MotionProp::Margin:
        case MotionProp::Gap:
        case MotionProp::FontSize:
        case MotionProp::LineHeight:
            return true;
        default:
            return false;
    }
}

std::optional<CssEasing> parse_easing(std::string_view raw) {
    const std::string name = lower_copy(trim(raw));
    if (name == "linear") {
        return CssEasing::Linear;
    }
    if (name == "ease") {
        return CssEasing::Ease;
    }
    if (name == "ease-in") {
        return CssEasing::EaseIn;
    }
    if (name == "ease-out") {
        return CssEasing::EaseOut;
    }
    if (name == "ease-in-out") {
        return CssEasing::EaseInOut;
    }
    return std::nullopt;
}

// Bare numbers are seconds, matching animation-duration. `ms` is 1/1000.
std::optional<float> parse_time(std::string_view raw) {
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
    const std::string suffix = lower_copy(trim(std::string_view(end)));
    if (suffix.empty() || suffix == "s") {
        return n;
    }
    if (suffix == "ms") {
        return n / 1000.0f;
    }
    return std::nullopt;
}

std::vector<std::string_view> split_comma(std::string_view value) {
    std::vector<std::string_view> parts;
    int depth = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c == '(') {
            ++depth;
        } else if (c == ')' && depth > 0) {
            --depth;
        } else if (c == ',' && depth == 0) {
            parts.push_back(trim(value.substr(start, i - start)));
            start = i + 1;
        }
    }
    parts.push_back(trim(value.substr(start)));
    return parts;
}

struct Tok {
    enum class Kind { Ident, Time, Number };
    Kind kind = Kind::Ident;
    std::string text;
    float number = 0.0f;
};

std::vector<Tok> tokenize(std::string_view raw) {
    std::vector<Tok> out;
    std::size_t i = 0;
    const std::string_view value = trim(raw);
    while (i < value.size()) {
        if (std::isspace(static_cast<unsigned char>(value[i])) != 0) {
            ++i;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(value[i])) != 0 || value[i] == '.' || value[i] == '+' ||
                value[i] == '-') {
            const std::size_t begin = i;
            ++i;
            while (i < value.size() &&
                    (std::isdigit(static_cast<unsigned char>(value[i])) != 0 || value[i] == '.')) {
                ++i;
            }
            const std::size_t unit = i;
            while (i < value.size() && std::isalpha(static_cast<unsigned char>(value[i])) != 0) {
                ++i;
            }
            const std::string token(value.substr(begin, i - begin));
            const float n = std::strtof(token.c_str(), nullptr);
            Tok tok;
            tok.number = n;
            tok.text = token;
            tok.kind = unit == i ? Tok::Kind::Number : Tok::Kind::Time;
            if (tok.kind == Tok::Kind::Time) {
                if (const auto seconds = parse_time(token)) {
                    tok.number = *seconds;
                }
            }
            out.push_back(std::move(tok));
            continue;
        }
        const std::size_t begin = i;
        while (i < value.size() &&
                (std::isalnum(static_cast<unsigned char>(value[i])) != 0 || value[i] == '-' || value[i] == '_')) {
            ++i;
        }
        if (begin == i) {
            ++i;
            continue;
        }
        Tok tok;
        tok.kind = Tok::Kind::Ident;
        tok.text = lower_copy(value.substr(begin, i - begin));
        out.push_back(std::move(tok));
    }
    return out;
}

struct TransitionItem {
    std::string property;
    float duration = 0.0f;
    float delay = 0.0f;
    CssEasing easing = CssEasing::Linear;
};

struct AnimationItem {
    std::string name;
    float duration = 0.0f;
    float delay = 0.0f;
    CssEasing easing = CssEasing::Linear;
    float iterations = 1.0f;
};

std::optional<TransitionItem> parse_transition_item(std::string_view raw) {
    const std::vector<Tok> toks = tokenize(raw);
    if (toks.empty()) {
        return std::nullopt;
    }
    TransitionItem item;
    int times = 0;
    bool saw_prop = false;
    for (const Tok& tok : toks) {
        if (tok.kind == Tok::Kind::Time || (tok.kind == Tok::Kind::Number && times == 0 && saw_prop)) {
            // A bare number after the property is a duration in seconds (engine time syntax).
            const float seconds = tok.kind == Tok::Kind::Time ? tok.number : tok.number;
            if (times == 0) {
                item.duration = seconds;
            } else {
                item.delay = seconds;
            }
            ++times;
            continue;
        }
        if (tok.kind != Tok::Kind::Ident) {
            continue;
        }
        if (const auto easing = parse_easing(tok.text)) {
            item.easing = *easing;
            continue;
        }
        if (!saw_prop) {
            item.property = tok.text;
            saw_prop = true;
        }
    }
    if (!saw_prop) {
        return std::nullopt;
    }
    return item;
}

std::optional<AnimationItem> parse_animation_item(std::string_view raw) {
    const std::vector<Tok> toks = tokenize(raw);
    if (toks.empty()) {
        return std::nullopt;
    }
    AnimationItem item;
    int times = 0;
    bool saw_name = false;
    for (const Tok& tok : toks) {
        if (tok.kind == Tok::Kind::Time) {
            if (times == 0) {
                item.duration = tok.number;
            } else {
                item.delay = tok.number;
            }
            ++times;
            continue;
        }
        if (tok.kind == Tok::Kind::Number) {
            if (times == 0 && !saw_name) {
                item.duration = tok.number;
                ++times;
            } else if (times == 0) {
                item.duration = tok.number;
                ++times;
            } else {
                item.iterations = tok.number;
            }
            continue;
        }
        if (const auto easing = parse_easing(tok.text)) {
            item.easing = *easing;
            continue;
        }
        if (tok.text == "infinite") {
            item.iterations = -1.0f;
            continue;
        }
        if (!saw_name) {
            item.name = tok.text;
            saw_name = true;
        }
    }
    if (!saw_name) {
        return std::nullopt;
    }
    return item;
}

void clear_transitions(ComputedStyle& style) {
    style.transition_properties.clear();
    style.transition_durations.clear();
    style.transition_delays.clear();
    style.transition_easings.clear();
}

void clear_animations(ComputedStyle& style) {
    style.animation_names.clear();
    style.animation_durations.clear();
    style.animation_delays.clear();
    style.animation_easings.clear();
    style.animation_iterations.clear();
}

std::vector<std::string> idents_of(std::string_view value) {
    std::vector<std::string> names;
    for (const std::string_view part : split_comma(value)) {
        const std::string name = lower_copy(trim(part));
        if (!name.empty()) {
            names.push_back(name);
        }
    }
    return names;
}

float em_of(const ComputedStyle& style, glm::vec2 basis) {
    return resolve_font_size(style.font_size, basis.x);
}

MotionValue read_length(const Length& length, float percent_basis, float em) {
    MotionValue value;
    value.present = true;
    value.length = length;
    value.px = resolve_length(length, percent_basis, em);
    return value;
}

MotionValue read_opt_length(const std::optional<Length>& length, float percent_basis, float em) {
    MotionValue value;
    if (!length) {
        return value;
    }
    value.present = true;
    value.opt_length = length;
    value.length = *length;
    value.px = resolve_length(*length, percent_basis, em);
    return value;
}

MotionValue read_insets(const LengthInsets& insets, glm::vec2 basis, float em) {
    MotionValue value;
    value.present = true;
    value.insets = insets;
    value.px = resolve_length(insets.top, basis.y, em);
    value.px_r = resolve_length(insets.right, basis.x, em);
    value.px_b = resolve_length(insets.bottom, basis.y, em);
    value.px_l = resolve_length(insets.left, basis.x, em);
    return value;
}

bool length_eq(const Length& a, const Length& b) {
    return a.value == b.value && a.unit == b.unit && a.calc.size() == b.calc.size();
}

bool insets_eq(const LengthInsets& a, const LengthInsets& b) {
    return length_eq(a.top, b.top) && length_eq(a.right, b.right) && length_eq(a.bottom, b.bottom) &&
            length_eq(a.left, b.left);
}

bool color_eq(glm::vec4 a, glm::vec4 b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

bool gradient_eq(const Gradient& a, const Gradient& b) {
    if (a.kind != b.kind || a.angle_deg != b.angle_deg || a.stops.size() != b.stops.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.stops.size(); ++i) {
        if (!color_eq(a.stops[i].color, b.stops[i].color) || a.stops[i].percent != b.stops[i].percent) {
            return false;
        }
    }
    return true;
}

MotionValue read_prop(const ComputedStyle& style, MotionProp prop, glm::vec2 basis) {
    const float em = em_of(style, basis);
    MotionValue value;
    switch (prop) {
        case MotionProp::Color:
            value.present = true;
            value.color = style.color;
            return value;
        case MotionProp::Background:
            value.present = true;
            value.color = style.background;
            value.has_gradient = style.background_gradient.has_value();
            value.gradient = style.background_gradient;
            return value;
        case MotionProp::Opacity:
            value.present = true;
            value.number = style.opacity;
            return value;
        case MotionProp::Visibility:
            value.present = true;
            value.flag = style.visible;
            return value;
        case MotionProp::Display:
            value.present = true;
            value.flag = style.display_none;
            return value;
        case MotionProp::Width:
            return read_opt_length(style.width, basis.x, em);
        case MotionProp::Height:
            return read_opt_length(style.height, basis.y, em);
        case MotionProp::MinWidth:
            return read_opt_length(style.min_width, basis.x, em);
        case MotionProp::MaxWidth:
            return read_opt_length(style.max_width, basis.x, em);
        case MotionProp::MinHeight:
            return read_opt_length(style.min_height, basis.y, em);
        case MotionProp::Padding:
            return read_insets(style.padding, basis, em);
        case MotionProp::Margin:
            return read_insets(style.margin, basis, em);
        case MotionProp::Gap:
            value = read_length(style.gap, basis.x, em);
            value.present = style.has_gap;
            value.flag = style.has_gap;
            return value;
        case MotionProp::FlexDirection:
            value.present = true;
            value.flag = style.has_direction;
            value.enumer = static_cast<std::uint8_t>(style.direction);
            return value;
        case MotionProp::AlignItems:
            value.present = true;
            value.enumer = static_cast<std::uint8_t>(style.align_items);
            return value;
        case MotionProp::JustifyContent:
            value.present = true;
            value.enumer = static_cast<std::uint8_t>(style.justify);
            return value;
        case MotionProp::TextAlign:
            value.present = true;
            value.enumer = static_cast<std::uint8_t>(style.text_align);
            return value;
        case MotionProp::WhiteSpace:
            value.present = true;
            value.enumer = static_cast<std::uint8_t>(style.white_space);
            return value;
        case MotionProp::UserSelect:
            value.present = true;
            value.enumer = static_cast<std::uint8_t>(style.user_select);
            return value;
        case MotionProp::BackgroundImage:
            value.present = true;
            value.image = style.background_image;
            value.has_gradient = style.background_gradient.has_value();
            value.gradient = style.background_gradient;
            return value;
        case MotionProp::BackgroundSlice:
            if (!style.background_slice) {
                return value;
            }
            value = read_insets(*style.background_slice, basis, em);
            value.flag = true;
            return value;
        case MotionProp::BackgroundRepeat:
            value.present = true;
            value.enumer = static_cast<std::uint8_t>(style.background_repeat);
            return value;
        case MotionProp::BorderRadius:
            return read_length(style.border_radius, basis.x, em);
        case MotionProp::BorderWidth:
            return read_length(style.border_width, basis.x, em);
        case MotionProp::BorderColor:
            value.present = true;
            value.color = style.border_color;
            return value;
        case MotionProp::FontSize:
            return read_length(style.font_size, basis.x, em);
        case MotionProp::LineHeight:
            value.present = true;
            value.line_height = style.line_height;
            value.enumer = static_cast<std::uint8_t>(style.line_height.kind);
            value.number = style.line_height.factor;
            if (style.line_height.kind == LineHeightKind::Length) {
                value.px = resolve_line_height(style.line_height, em, em);
            }
            return value;
        case MotionProp::FontFamily:
            value.present = true;
            value.asset = style.font_family;
            return value;
        case MotionProp::ZIndex:
            value.present = true;
            value.integer = style.z_index;
            return value;
        case MotionProp::Position:
            value.present = true;
            value.enumer = static_cast<std::uint8_t>(style.position);
            return value;
        case MotionProp::Top:
            return read_opt_length(style.inset_top, basis.y, em);
        case MotionProp::Right:
            return read_opt_length(style.inset_right, basis.x, em);
        case MotionProp::Bottom:
            return read_opt_length(style.inset_bottom, basis.y, em);
        case MotionProp::Left:
            return read_opt_length(style.inset_left, basis.x, em);
        case MotionProp::Transform:
            value.present = true;
            value.number = style.rotation_deg;
            value.number2 = style.scale;
            return value;
        case MotionProp::X1:
            return read_length(style.x1, basis.x, em);
        case MotionProp::Y1:
            return read_length(style.y1, basis.y, em);
        case MotionProp::X2:
            return read_length(style.x2, basis.x, em);
        case MotionProp::Y2:
            return read_length(style.y2, basis.y, em);
        case MotionProp::Stroke:
            value.present = true;
            value.color = style.stroke;
            return value;
        case MotionProp::StrokeWidth:
            return read_length(style.stroke_width, basis.x, em);
        case MotionProp::Overflow:
            value.present = true;
            value.enumer = static_cast<std::uint8_t>(style.overflow_x);
            value.enumer2 = static_cast<std::uint8_t>(style.overflow_y);
            value.flag = style.has_overflow_x;
            value.flag2 = style.has_overflow_y;
            return value;
        case MotionProp::OverflowX:
            value.present = true;
            value.enumer = static_cast<std::uint8_t>(style.overflow_x);
            value.flag = style.has_overflow_x;
            return value;
        case MotionProp::OverflowY:
            value.present = true;
            value.enumer = static_cast<std::uint8_t>(style.overflow_y);
            value.flag = style.has_overflow_y;
            return value;
        case MotionProp::ScrollbarWidth:
            value = read_opt_length(style.scrollbar_width, basis.x, em);
            value.flag = style.has_scrollbar_width;
            value.present = style.has_scrollbar_width;
            return value;
        case MotionProp::ScrollbarColor:
            value.present = style.has_scrollbar_thumb_color || style.has_scrollbar_track_color;
            value.color = style.scrollbar_thumb_color;
            value.color2 = style.scrollbar_track_color;
            value.flag = style.has_scrollbar_thumb_color;
            value.flag2 = style.has_scrollbar_track_color;
            return value;
        case MotionProp::ScrollbarThumbColor:
            value.present = true;
            value.color = style.scrollbar_thumb_color;
            value.flag = style.has_scrollbar_thumb_color;
            return value;
        case MotionProp::ScrollbarTrackColor:
            value.present = true;
            value.color = style.scrollbar_track_color;
            value.flag = style.has_scrollbar_track_color;
            return value;
        case MotionProp::ScrollbarThumbHoverColor:
            value.present = true;
            value.color = style.scrollbar_thumb_hover_color;
            value.flag = style.has_scrollbar_thumb_hover_color;
            return value;
        case MotionProp::ScrollbarBorderRadius:
            value = read_length(style.scrollbar_border_radius, basis.x, em);
            value.flag = style.has_scrollbar_border_radius;
            return value;
        case MotionProp::SelectionColor:
            value.present = true;
            value.color = style.selection_color;
            value.flag = style.has_selection_color;
            return value;
        case MotionProp::Count:
            break;
    }
    return value;
}

Length px_length(float px) {
    return Length{px, LengthUnit::Px};
}

LengthInsets px_insets(const MotionValue& value) {
    return LengthInsets{px_length(value.px), px_length(value.px_r), px_length(value.px_b), px_length(value.px_l)};
}

void write_style(ComputedStyle& style, MotionProp prop, const MotionValue& value) {
    switch (prop) {
        case MotionProp::Color:
            style.color = value.color;
            break;
        case MotionProp::Background:
            style.background = value.color;
            style.background_gradient = value.has_gradient ? value.gradient : std::nullopt;
            break;
        case MotionProp::Opacity:
            style.opacity = value.number;
            break;
        case MotionProp::Visibility:
            style.visible = value.flag;
            break;
        case MotionProp::Display:
            style.display_none = value.flag;
            break;
        case MotionProp::Width:
            style.width = value.present ? std::optional<Length>{px_length(value.px)} : std::nullopt;
            break;
        case MotionProp::Height:
            style.height = value.present ? std::optional<Length>{px_length(value.px)} : std::nullopt;
            break;
        case MotionProp::MinWidth:
            style.min_width = value.present ? std::optional<Length>{px_length(value.px)} : std::nullopt;
            break;
        case MotionProp::MaxWidth:
            style.max_width = value.present ? std::optional<Length>{px_length(value.px)} : std::nullopt;
            break;
        case MotionProp::MinHeight:
            style.min_height = value.present ? std::optional<Length>{px_length(value.px)} : std::nullopt;
            break;
        case MotionProp::Padding:
            style.padding = px_insets(value);
            break;
        case MotionProp::Margin:
            style.margin = px_insets(value);
            break;
        case MotionProp::Gap:
            style.gap = px_length(value.px);
            style.has_gap = value.present;
            break;
        case MotionProp::FlexDirection:
            style.direction = static_cast<StackDirection>(value.enumer);
            style.has_direction = value.flag;
            break;
        case MotionProp::AlignItems:
            style.align_items = static_cast<UiAlign>(value.enumer);
            break;
        case MotionProp::JustifyContent:
            style.justify = static_cast<UiAlign>(value.enumer);
            break;
        case MotionProp::TextAlign:
            style.text_align = static_cast<UiAlign>(value.enumer);
            break;
        case MotionProp::WhiteSpace:
            style.white_space = static_cast<WhiteSpace>(value.enumer);
            break;
        case MotionProp::UserSelect:
            style.user_select = static_cast<UserSelect>(value.enumer);
            break;
        case MotionProp::BackgroundImage:
            style.background_image = value.image;
            style.background_gradient = value.has_gradient ? value.gradient : std::nullopt;
            break;
        case MotionProp::BackgroundSlice:
            style.background_slice = value.present ? std::optional<LengthInsets>{px_insets(value)} : std::nullopt;
            break;
        case MotionProp::BackgroundRepeat:
            style.background_repeat = static_cast<BackgroundRepeat>(value.enumer);
            break;
        case MotionProp::BorderRadius:
            style.border_radius = px_length(value.px);
            break;
        case MotionProp::BorderWidth:
            style.border_width = px_length(value.px);
            break;
        case MotionProp::BorderColor:
            style.border_color = value.color;
            break;
        case MotionProp::FontSize:
            style.font_size = px_length(value.px);
            break;
        case MotionProp::LineHeight:
            style.line_height = value.line_height;
            if (value.line_height.kind == LineHeightKind::Length) {
                style.line_height.length = px_length(value.px);
            }
            break;
        case MotionProp::FontFamily:
            style.font_family = value.asset;
            break;
        case MotionProp::ZIndex:
            style.z_index = value.integer;
            break;
        case MotionProp::Position:
            style.position = static_cast<PositionMode>(value.enumer);
            break;
        case MotionProp::Top:
            style.inset_top = value.present ? std::optional<Length>{px_length(value.px)} : std::nullopt;
            break;
        case MotionProp::Right:
            style.inset_right = value.present ? std::optional<Length>{px_length(value.px)} : std::nullopt;
            break;
        case MotionProp::Bottom:
            style.inset_bottom = value.present ? std::optional<Length>{px_length(value.px)} : std::nullopt;
            break;
        case MotionProp::Left:
            style.inset_left = value.present ? std::optional<Length>{px_length(value.px)} : std::nullopt;
            break;
        case MotionProp::Transform:
            style.rotation_deg = value.number;
            style.scale = value.number2;
            break;
        case MotionProp::X1:
            style.x1 = px_length(value.px);
            break;
        case MotionProp::Y1:
            style.y1 = px_length(value.px);
            break;
        case MotionProp::X2:
            style.x2 = px_length(value.px);
            break;
        case MotionProp::Y2:
            style.y2 = px_length(value.px);
            break;
        case MotionProp::Stroke:
            style.stroke = value.color;
            break;
        case MotionProp::StrokeWidth:
            style.stroke_width = px_length(value.px);
            break;
        case MotionProp::Overflow:
            style.overflow_x = static_cast<Overflow>(value.enumer);
            style.overflow_y = static_cast<Overflow>(value.enumer2);
            style.has_overflow_x = value.flag;
            style.has_overflow_y = value.flag2;
            break;
        case MotionProp::OverflowX:
            style.overflow_x = static_cast<Overflow>(value.enumer);
            style.has_overflow_x = value.flag;
            break;
        case MotionProp::OverflowY:
            style.overflow_y = static_cast<Overflow>(value.enumer);
            style.has_overflow_y = value.flag;
            break;
        case MotionProp::ScrollbarWidth:
            style.has_scrollbar_width = value.present;
            style.scrollbar_width = value.present ? std::optional<Length>{px_length(value.px)} : std::nullopt;
            break;
        case MotionProp::ScrollbarColor:
            style.scrollbar_thumb_color = value.color;
            style.scrollbar_track_color = value.color2;
            style.has_scrollbar_thumb_color = value.flag;
            style.has_scrollbar_track_color = value.flag2;
            break;
        case MotionProp::ScrollbarThumbColor:
            style.scrollbar_thumb_color = value.color;
            style.has_scrollbar_thumb_color = value.flag || value.present;
            break;
        case MotionProp::ScrollbarTrackColor:
            style.scrollbar_track_color = value.color;
            style.has_scrollbar_track_color = value.flag || value.present;
            break;
        case MotionProp::ScrollbarThumbHoverColor:
            style.scrollbar_thumb_hover_color = value.color;
            style.has_scrollbar_thumb_hover_color = value.flag || value.present;
            break;
        case MotionProp::ScrollbarBorderRadius:
            style.scrollbar_border_radius = px_length(value.px);
            style.has_scrollbar_border_radius = true;
            break;
        case MotionProp::SelectionColor:
            style.selection_color = value.color;
            style.has_selection_color = value.flag || value.present;
            break;
        case MotionProp::Count:
            break;
    }
}

bool near_px(float a, float b) {
    return std::fabs(a - b) <= 0.05f;
}

bool opt_px_differs(const std::optional<Length>& current, const MotionValue& shown) {
    if (!shown.present) {
        return current.has_value();
    }
    if (!current || !current->calc.empty() || current->unit != LengthUnit::Px) {
        return true;
    }
    return !near_px(current->value, shown.px);
}

bool insets_differs(const LengthInsets& current, const MotionValue& shown) {
    if (!current.top.calc.empty() || current.top.unit != LengthUnit::Px || !near_px(current.top.value, shown.px)) {
        return true;
    }
    if (!current.right.calc.empty() || current.right.unit != LengthUnit::Px ||
            !near_px(current.right.value, shown.px_r)) {
        return true;
    }
    if (!current.bottom.calc.empty() || current.bottom.unit != LengthUnit::Px ||
            !near_px(current.bottom.value, shown.px_b)) {
        return true;
    }
    return !current.left.calc.empty() || current.left.unit != LengthUnit::Px ||
            !near_px(current.left.value, shown.px_l);
}

bool layout_differs(const Element& element, MotionProp prop, const MotionValue& shown) {
    switch (prop) {
        case MotionProp::Display:
            return element.display_none != shown.flag;
        case MotionProp::Width:
            return opt_px_differs(element.width, shown);
        case MotionProp::Height:
            return opt_px_differs(element.height, shown);
        case MotionProp::MinWidth:
            return opt_px_differs(element.min_width, shown);
        case MotionProp::MaxWidth:
            return opt_px_differs(element.max_width, shown);
        case MotionProp::MinHeight:
            return opt_px_differs(element.min_height, shown);
        case MotionProp::Padding:
            return insets_differs(element.padding, shown);
        case MotionProp::Margin:
            return insets_differs(element.margin, shown);
        case MotionProp::Gap:
            return !shown.present ? false : element.gap.unit != LengthUnit::Px || !near_px(element.gap.value, shown.px);
        case MotionProp::FlexDirection:
            return element.direction != static_cast<StackDirection>(shown.enumer);
        case MotionProp::AlignItems:
            return element.align_items != static_cast<UiAlign>(shown.enumer);
        case MotionProp::JustifyContent:
            return element.justify != static_cast<UiAlign>(shown.enumer);
        case MotionProp::WhiteSpace:
            return element.white_space != static_cast<WhiteSpace>(shown.enumer);
        case MotionProp::FontSize:
            return element.font_size.unit != LengthUnit::Px || !near_px(element.font_size.value, shown.px);
        case MotionProp::LineHeight:
            return element.line_height.kind != shown.line_height.kind ||
                    element.line_height.factor != shown.line_height.factor ||
                    !length_eq(element.line_height.length, shown.line_height.length);
        case MotionProp::FontFamily:
            return element.font_family != shown.asset;
        case MotionProp::Position:
            return element.position != static_cast<PositionMode>(shown.enumer);
        case MotionProp::Top:
            return opt_px_differs(element.inset_top, shown);
        case MotionProp::Right:
            return opt_px_differs(element.inset_right, shown);
        case MotionProp::Bottom:
            return opt_px_differs(element.inset_bottom, shown);
        case MotionProp::Left:
            return opt_px_differs(element.inset_left, shown);
        default:
            return false;
    }
}

void write_element(Element& element, MotionProp prop, const MotionValue& value) {
    switch (prop) {
        case MotionProp::Visibility:
            element.visible = value.flag;
            break;
        case MotionProp::Display:
            element.display_none = value.flag;
            if (value.flag) {
                element.layout_rect = render::Rect{};
            }
            break;
        case MotionProp::Width:
            element.width = value.present ? std::optional<Length>{px_length(value.px)} : std::nullopt;
            break;
        case MotionProp::Height:
            element.height = value.present ? std::optional<Length>{px_length(value.px)} : std::nullopt;
            break;
        case MotionProp::MinWidth:
            element.min_width = value.present ? std::optional<Length>{px_length(value.px)} : std::nullopt;
            break;
        case MotionProp::MaxWidth:
            element.max_width = value.present ? std::optional<Length>{px_length(value.px)} : std::nullopt;
            break;
        case MotionProp::MinHeight:
            element.min_height = value.present ? std::optional<Length>{px_length(value.px)} : std::nullopt;
            break;
        case MotionProp::Padding:
            element.padding = px_insets(value);
            break;
        case MotionProp::Margin:
            element.margin = px_insets(value);
            break;
        case MotionProp::Gap:
            if (value.present) {
                element.gap = px_length(value.px);
            }
            break;
        case MotionProp::FlexDirection:
            if (value.flag) {
                element.direction = static_cast<StackDirection>(value.enumer);
            }
            break;
        case MotionProp::AlignItems:
            element.align_items = static_cast<UiAlign>(value.enumer);
            break;
        case MotionProp::JustifyContent:
            element.justify = static_cast<UiAlign>(value.enumer);
            break;
        case MotionProp::TextAlign:
            element.text_align = static_cast<UiAlign>(value.enumer);
            break;
        case MotionProp::WhiteSpace:
            element.white_space = static_cast<WhiteSpace>(value.enumer);
            break;
        case MotionProp::UserSelect:
            element.user_select = static_cast<UserSelect>(value.enumer);
            break;
        case MotionProp::FontSize:
            element.font_size = px_length(value.px);
            break;
        case MotionProp::LineHeight:
            element.line_height = value.line_height;
            if (value.line_height.kind == LineHeightKind::Length) {
                element.line_height.length = px_length(value.px);
            }
            break;
        case MotionProp::FontFamily:
            element.font_family = value.asset;
            break;
        case MotionProp::ZIndex:
            element.z_index = value.integer;
            break;
        case MotionProp::Position:
            element.position = static_cast<PositionMode>(value.enumer);
            break;
        case MotionProp::Top:
            element.inset_top = value.present ? std::optional<Length>{px_length(value.px)} : std::nullopt;
            break;
        case MotionProp::Right:
            element.inset_right = value.present ? std::optional<Length>{px_length(value.px)} : std::nullopt;
            break;
        case MotionProp::Bottom:
            element.inset_bottom = value.present ? std::optional<Length>{px_length(value.px)} : std::nullopt;
            break;
        case MotionProp::Left:
            element.inset_left = value.present ? std::optional<Length>{px_length(value.px)} : std::nullopt;
            break;
        case MotionProp::Transform:
            element.rotation_deg = value.number;
            element.scale = value.number2;
            break;
        case MotionProp::Overflow:
            if (value.flag) {
                element.overflow_x = static_cast<Overflow>(value.enumer);
            }
            if (value.flag2) {
                element.overflow_y = static_cast<Overflow>(value.enumer2);
            }
            break;
        case MotionProp::OverflowX:
            if (value.flag) {
                element.overflow_x = static_cast<Overflow>(value.enumer);
            }
            break;
        case MotionProp::OverflowY:
            if (value.flag) {
                element.overflow_y = static_cast<Overflow>(value.enumer);
            }
            break;
        case MotionProp::ScrollbarWidth:
            if (value.present) {
                element.scrollbar_width = px_length(value.px);
            }
            break;
        case MotionProp::ScrollbarColor:
            if (value.flag) {
                element.scrollbar_thumb_color = value.color;
            }
            if (value.flag2) {
                element.scrollbar_track_color = value.color2;
            }
            break;
        case MotionProp::ScrollbarThumbColor:
            element.scrollbar_thumb_color = value.color;
            break;
        case MotionProp::ScrollbarTrackColor:
            element.scrollbar_track_color = value.color;
            break;
        case MotionProp::ScrollbarThumbHoverColor:
            element.scrollbar_thumb_hover_color = value.color;
            break;
        case MotionProp::ScrollbarBorderRadius:
            element.scrollbar_border_radius = px_length(value.px);
            break;
        case MotionProp::SelectionColor:
            element.selection_color = value.color;
            break;
        default:
            break;
    }
}

bool same_spec(MotionProp prop, const MotionValue& a, const MotionValue& b) {
    switch (prop) {
        case MotionProp::Color:
        case MotionProp::BorderColor:
        case MotionProp::Stroke:
        case MotionProp::SelectionColor:
        case MotionProp::ScrollbarThumbColor:
        case MotionProp::ScrollbarTrackColor:
        case MotionProp::ScrollbarThumbHoverColor:
            return color_eq(a.color, b.color);
        case MotionProp::ScrollbarColor:
            return color_eq(a.color, b.color) && color_eq(a.color2, b.color2);
        case MotionProp::Background:
            if (a.has_gradient != b.has_gradient) {
                return false;
            }
            if (a.has_gradient) {
                return a.gradient && b.gradient && gradient_eq(*a.gradient, *b.gradient);
            }
            return color_eq(a.color, b.color);
        case MotionProp::BackgroundImage:
            return a.image == b.image && a.has_gradient == b.has_gradient &&
                    (!a.has_gradient || (a.gradient && b.gradient && gradient_eq(*a.gradient, *b.gradient)));
        case MotionProp::Opacity:
        case MotionProp::Transform:
            return a.number == b.number && a.number2 == b.number2;
        case MotionProp::ZIndex:
            return a.integer == b.integer;
        case MotionProp::Visibility:
        case MotionProp::Display:
            return a.flag == b.flag;
        case MotionProp::Width:
        case MotionProp::Height:
        case MotionProp::MinWidth:
        case MotionProp::MaxWidth:
        case MotionProp::MinHeight:
        case MotionProp::Top:
        case MotionProp::Right:
        case MotionProp::Bottom:
        case MotionProp::Left:
        case MotionProp::ScrollbarWidth:
            if (a.present != b.present) {
                return false;
            }
            return !a.present || length_eq(a.length, b.length);
        case MotionProp::Padding:
        case MotionProp::Margin:
        case MotionProp::BackgroundSlice:
            return a.present == b.present && (!a.present || insets_eq(a.insets, b.insets));
        case MotionProp::Gap:
        case MotionProp::BorderRadius:
        case MotionProp::BorderWidth:
        case MotionProp::FontSize:
        case MotionProp::X1:
        case MotionProp::Y1:
        case MotionProp::X2:
        case MotionProp::Y2:
        case MotionProp::StrokeWidth:
        case MotionProp::ScrollbarBorderRadius:
            return a.present == b.present && length_eq(a.length, b.length);
        case MotionProp::LineHeight:
            return a.enumer == b.enumer && a.number == b.number &&
                    length_eq(a.line_height.length, b.line_height.length);
        case MotionProp::FontFamily:
            return a.asset == b.asset;
        case MotionProp::FlexDirection:
        case MotionProp::AlignItems:
        case MotionProp::JustifyContent:
        case MotionProp::TextAlign:
        case MotionProp::WhiteSpace:
        case MotionProp::UserSelect:
        case MotionProp::BackgroundRepeat:
        case MotionProp::Position:
        case MotionProp::OverflowX:
        case MotionProp::OverflowY:
            return a.enumer == b.enumer && a.flag == b.flag;
        case MotionProp::Overflow:
            return a.enumer == b.enumer && a.enumer2 == b.enumer2 && a.flag == b.flag && a.flag2 == b.flag2;
        case MotionProp::Count:
            break;
    }
    return false;
}

bool can_lerp(MotionProp prop, const MotionValue& from, const MotionValue& to) {
    switch (prop) {
        case MotionProp::Color:
        case MotionProp::BorderColor:
        case MotionProp::Stroke:
        case MotionProp::SelectionColor:
        case MotionProp::ScrollbarThumbColor:
        case MotionProp::ScrollbarTrackColor:
        case MotionProp::ScrollbarThumbHoverColor:
        case MotionProp::ScrollbarColor:
        case MotionProp::Opacity:
        case MotionProp::ZIndex:
        case MotionProp::Transform:
            return true;
        case MotionProp::Background:
            return !from.has_gradient && !to.has_gradient;
        case MotionProp::Width:
        case MotionProp::Height:
        case MotionProp::MinWidth:
        case MotionProp::MaxWidth:
        case MotionProp::MinHeight:
        case MotionProp::Top:
        case MotionProp::Right:
        case MotionProp::Bottom:
        case MotionProp::Left:
        case MotionProp::ScrollbarWidth:
        case MotionProp::BackgroundSlice:
            return from.present && to.present;
        case MotionProp::Padding:
        case MotionProp::Margin:
        case MotionProp::Gap:
        case MotionProp::BorderRadius:
        case MotionProp::BorderWidth:
        case MotionProp::FontSize:
        case MotionProp::X1:
        case MotionProp::Y1:
        case MotionProp::X2:
        case MotionProp::Y2:
        case MotionProp::StrokeWidth:
        case MotionProp::ScrollbarBorderRadius:
            return from.present && to.present;
        case MotionProp::LineHeight:
            return from.enumer == to.enumer && from.enumer != static_cast<std::uint8_t>(LineHeightKind::Normal);
        default:
            return false;
    }
}

float bezier_component(float t, float c1, float c2) {
    const float u = 1.0f - t;
    return 3.0f * u * u * t * c1 + 3.0f * u * t * t * c2 + t * t * t;
}

float bezier_derivative(float t, float c1, float c2) {
    const float u = 1.0f - t;
    return 3.0f * u * u * c1 + 6.0f * u * t * (c2 - c1) + 3.0f * t * t * (1.0f - c2);
}

float cubic_bezier(float x, float x1, float y1, float x2, float y2) {
    if (x <= 0.0f) {
        return 0.0f;
    }
    if (x >= 1.0f) {
        return 1.0f;
    }
    float t = x;
    for (int i = 0; i < 8; ++i) {
        const float dx = bezier_derivative(t, x1, x2);
        if (std::fabs(dx) < 1.0e-5f) {
            break;
        }
        t = std::clamp(t - (bezier_component(t, x1, x2) - x) / dx, 0.0f, 1.0f);
    }
    return bezier_component(t, y1, y2);
}

float apply_easing(CssEasing easing, float t) {
    switch (easing) {
        case CssEasing::Ease:
            return cubic_bezier(t, 0.25f, 0.1f, 0.25f, 1.0f);
        case CssEasing::EaseIn:
            return cubic_bezier(t, 0.42f, 0.0f, 1.0f, 1.0f);
        case CssEasing::EaseOut:
            return cubic_bezier(t, 0.0f, 0.0f, 0.58f, 1.0f);
        case CssEasing::EaseInOut:
            return cubic_bezier(t, 0.42f, 0.0f, 0.58f, 1.0f);
        case CssEasing::Linear:
            break;
    }
    return t;
}

glm::vec4 mix_color(glm::vec4 from, glm::vec4 to, float t) {
    return from + (to - from) * t;
}

MotionValue lerp_value(MotionProp prop, const MotionValue& from, const MotionValue& to, float t) {
    if (!can_lerp(prop, from, to)) {
        return t < 0.5f ? from : to;
    }
    MotionValue out = to;
    switch (prop) {
        case MotionProp::Color:
        case MotionProp::BorderColor:
        case MotionProp::Stroke:
        case MotionProp::SelectionColor:
        case MotionProp::ScrollbarThumbColor:
        case MotionProp::ScrollbarTrackColor:
        case MotionProp::ScrollbarThumbHoverColor:
            out.color = mix_color(from.color, to.color, t);
            break;
        case MotionProp::ScrollbarColor:
            out.color = mix_color(from.color, to.color, t);
            out.color2 = mix_color(from.color2, to.color2, t);
            break;
        case MotionProp::Background:
            out.color = mix_color(from.color, to.color, t);
            out.has_gradient = false;
            out.gradient.reset();
            break;
        case MotionProp::Opacity:
            out.number = from.number + (to.number - from.number) * t;
            break;
        case MotionProp::ZIndex:
            out.integer = static_cast<int>(std::lround(static_cast<float>(from.integer) +
                    (static_cast<float>(to.integer - from.integer) * t)));
            break;
        case MotionProp::Transform:
            out.number = from.number + (to.number - from.number) * t;
            out.number2 = from.number2 + (to.number2 - from.number2) * t;
            break;
        case MotionProp::Padding:
        case MotionProp::Margin:
        case MotionProp::BackgroundSlice:
            out.px = from.px + (to.px - from.px) * t;
            out.px_r = from.px_r + (to.px_r - from.px_r) * t;
            out.px_b = from.px_b + (to.px_b - from.px_b) * t;
            out.px_l = from.px_l + (to.px_l - from.px_l) * t;
            out.insets = px_insets(out);
            out.present = true;
            break;
        case MotionProp::LineHeight:
            if (from.enumer == static_cast<std::uint8_t>(LineHeightKind::Factor)) {
                out.number = from.number + (to.number - from.number) * t;
                out.line_height.kind = LineHeightKind::Factor;
                out.line_height.factor = out.number;
            } else {
                out.px = from.px + (to.px - from.px) * t;
                out.line_height.kind = LineHeightKind::Length;
                out.line_height.length = px_length(out.px);
            }
            break;
        default:
            out.px = from.px + (to.px - from.px) * t;
            out.length = px_length(out.px);
            out.opt_length = px_length(out.px);
            out.present = true;
            break;
    }
    return out;
}

const Keyframes* find_keyframes(const Stylesheet& sheet, std::string_view name) {
    for (const Keyframes& keyframes : sheet.keyframes) {
        if (keyframes.name == name) {
            return &keyframes;
        }
    }
    return nullptr;
}

std::optional<MotionValue> sample_prop(const Keyframes& keyframes, MotionProp prop, float t, glm::vec2 basis) {
    struct Stop {
        float offset = 0.0f;
        MotionValue value;
    };
    std::vector<Stop> stops;
    for (const KeyframeStop& stop : keyframes.stops) {
        bool sets = false;
        for (const CssDeclaration& decl : stop.declarations) {
            if (decl_sets(decl.property, prop)) {
                sets = true;
                break;
            }
        }
        if (!sets) {
            continue;
        }
        ComputedStyle style;
        for (const CssDeclaration& decl : stop.declarations) {
            if (decl_sets(decl.property, prop)) {
                apply_style_declaration(style, decl);
            }
        }
        stops.push_back(Stop{stop.offset, read_prop(style, prop, basis)});
    }
    if (stops.empty()) {
        return std::nullopt;
    }
    std::sort(stops.begin(), stops.end(), [](const Stop& a, const Stop& b) { return a.offset < b.offset; });
    t = std::clamp(t, 0.0f, 1.0f);
    if (t <= stops.front().offset) {
        return stops.front().value;
    }
    if (t >= stops.back().offset) {
        return stops.back().value;
    }
    for (std::size_t i = 0; i + 1 < stops.size(); ++i) {
        if (t > stops[i + 1].offset) {
            continue;
        }
        const float span = stops[i + 1].offset - stops[i].offset;
        const float u = span > 0.0f ? (t - stops[i].offset) / span : 0.0f;
        return lerp_value(prop, stops[i].value, stops[i + 1].value, u);
    }
    return stops.back().value;
}

std::vector<MotionProp> keyframe_props(const Keyframes& keyframes) {
    std::vector<MotionProp> props;
    for (const KeyframeStop& stop : keyframes.stops) {
        for (const CssDeclaration& decl : stop.declarations) {
            if (const auto prop = prop_from_name(decl.property)) {
                if (std::find(props.begin(), props.end(), *prop) == props.end()) {
                    props.push_back(*prop);
                }
            }
        }
    }
    return props;
}

// Returns the sample progress, or < 0 while delay is still running (underlying style shows).
float animation_progress(const AnimationRuntime& player) {
    if (player.elapsed < player.delay) {
        return -1.0f;
    }
    const float local = player.elapsed - player.delay;
    if (player.duration <= 0.0f) {
        return 0.0f;
    }
    if (player.iterations < 0.0f) {
        const float cycles = local / player.duration;
        return cycles - std::floor(cycles);
    }
    const float end = player.duration * std::max(player.iterations, 0.0f);
    if (local >= end) {
        return 1.0f;
    }
    const float cycle_pos = local - std::floor(local / player.duration) * player.duration;
    return cycle_pos / player.duration;
}

bool animation_running(const AnimationRuntime& player) {
    if (player.elapsed < player.delay) {
        return false;
    }
    if (player.iterations < 0.0f) {
        return true;
    }
    const float end = player.delay + player.duration * std::max(player.iterations, 0.0f);
    return player.elapsed < end;
}

struct TransitionSpec {
    MotionProp prop = MotionProp::Opacity;
    float duration = 0.0f;
    float delay = 0.0f;
    CssEasing easing = CssEasing::Linear;
};

void upsert_spec(std::vector<TransitionSpec>& specs, TransitionSpec spec) {
    for (TransitionSpec& existing : specs) {
        if (existing.prop == spec.prop) {
            existing = spec;
            return;
        }
    }
    specs.push_back(spec);
}

std::vector<TransitionSpec> transition_specs(const ComputedStyle& style) {
    std::vector<TransitionSpec> specs;
    for (std::size_t i = 0; i < style.transition_properties.size(); ++i) {
        const std::string& name = style.transition_properties[i];
        const float duration = list_last(style.transition_durations, i, 0.0f);
        const float delay = list_last(style.transition_delays, i, 0.0f);
        const CssEasing easing = list_last(style.transition_easings, i, CssEasing::Linear);
        if (name == "none") {
            continue;
        }
        if (name == "all") {
            for (const MotionProp prop : kAllProps) {
                upsert_spec(specs, TransitionSpec{prop, duration, delay, easing});
            }
            continue;
        }
        if (const auto prop = prop_from_name(name)) {
            upsert_spec(specs, TransitionSpec{*prop, duration, delay, easing});
        }
    }
    return specs;
}

void upsert_shown(std::vector<ShownMotion>& shown, MotionProp prop, MotionValue value, bool running) {
    for (ShownMotion& entry : shown) {
        if (entry.prop == prop) {
            entry.value = std::move(value);
            entry.running = running;
            return;
        }
    }
    shown.push_back(ShownMotion{prop, std::move(value), running});
}

bool shown_has(const std::vector<ShownMotion>& shown, MotionProp prop) {
    for (const ShownMotion& entry : shown) {
        if (entry.prop == prop) {
            return true;
        }
    }
    return false;
}

float transition_u(const TransitionRuntime& player) {
    if (player.elapsed < player.delay || player.duration <= 0.0f) {
        return player.elapsed < player.delay ? 0.0f : 1.0f;
    }
    const float u = (player.elapsed - player.delay) / player.duration;
    return apply_easing(player.easing, std::clamp(u, 0.0f, 1.0f));
}

}  // namespace

bool is_motion_declaration(std::string_view property) {
    return property == "transition" || property == "transition-property" || property == "transition-duration" ||
            property == "transition-delay" || property == "transition-timing-function" || property == "animation" ||
            property == "animation-name" || property == "animation-duration" || property == "animation-delay" ||
            property == "animation-timing-function" || property == "animation-iteration-count";
}

std::vector<std::string> validate_motion_value(std::string_view property, std::string_view value) {
    std::vector<std::string> warnings;
    if (property == "transition-timing-function" || property == "animation-timing-function") {
        for (const std::string_view part : split_comma(value)) {
            if (trim(part).empty()) {
                continue;
            }
            if (!parse_easing(part)) {
                warnings.emplace_back("unknown easing: " + std::string(trim(part)));
            }
        }
        return warnings;
    }
    if (property == "transition-property") {
        for (const std::string& name : idents_of(value)) {
            if (name == "all" || name == "none") {
                continue;
            }
            if (!prop_from_name(name)) {
                warnings.emplace_back("unknown transition property: " + name);
            }
        }
        return warnings;
    }
    if (property == "transition") {
        for (const std::string_view part : split_comma(value)) {
            if (trim(part).empty()) {
                continue;
            }
            bool saw_prop = false;
            for (const Tok& tok : tokenize(part)) {
                if (tok.kind != Tok::Kind::Ident) {
                    continue;
                }
                if (parse_easing(tok.text)) {
                    continue;
                }
                if (!saw_prop) {
                    saw_prop = true;
                    if (tok.text != "all" && tok.text != "none" && !prop_from_name(tok.text)) {
                        warnings.emplace_back("unknown transition property: " + tok.text);
                    }
                    continue;
                }
                warnings.emplace_back("unknown easing: " + tok.text);
            }
        }
        return warnings;
    }
    if (property == "animation") {
        for (const std::string_view part : split_comma(value)) {
            if (trim(part).empty()) {
                continue;
            }
            bool saw_name = false;
            for (const Tok& tok : tokenize(part)) {
                if (tok.kind != Tok::Kind::Ident) {
                    continue;
                }
                if (!saw_name) {
                    saw_name = true;
                    continue;
                }
                if (parse_easing(tok.text) || tok.text == "infinite" || tok.text == "none") {
                    continue;
                }
                warnings.emplace_back("unknown easing: " + tok.text);
            }
        }
        return warnings;
    }
    if (property == "animation-iteration-count") {
        for (const std::string_view part : split_comma(value)) {
            const std::string name = lower_copy(trim(part));
            if (name.empty() || name == "infinite") {
                continue;
            }
            char* end = nullptr;
            std::strtof(name.c_str(), &end);
            if (end == name.c_str() || *end != '\0') {
                warnings.emplace_back("invalid animation-iteration-count: " + name);
            }
        }
    }
    return warnings;
}

void apply_motion_declaration(ComputedStyle& style, std::string_view property, std::string_view value) {
    if (property == "transition") {
        clear_transitions(style);
        if (lower_copy(trim(value)) == "none") {
            return;
        }
        for (const std::string_view part : split_comma(value)) {
            const auto item = parse_transition_item(part);
            if (!item || item->property == "none") {
                if (item && item->property == "none") {
                    clear_transitions(style);
                }
                continue;
            }
            style.transition_properties.push_back(item->property);
            style.transition_durations.push_back(item->duration);
            style.transition_delays.push_back(item->delay);
            style.transition_easings.push_back(item->easing);
        }
        return;
    }
    if (property == "transition-property") {
        style.transition_properties.clear();
        for (const std::string& name : idents_of(value)) {
            if (name == "none") {
                style.transition_properties.clear();
                return;
            }
            style.transition_properties.push_back(name);
        }
        return;
    }
    if (property == "transition-duration" || property == "animation-duration") {
        std::vector<float>& dest =
                property == "transition-duration" ? style.transition_durations : style.animation_durations;
        dest.clear();
        for (const std::string_view part : split_comma(value)) {
            if (const auto seconds = parse_time(part)) {
                dest.push_back(*seconds);
            }
        }
        return;
    }
    if (property == "transition-delay" || property == "animation-delay") {
        std::vector<float>& dest = property == "transition-delay" ? style.transition_delays : style.animation_delays;
        dest.clear();
        for (const std::string_view part : split_comma(value)) {
            if (const auto seconds = parse_time(part)) {
                dest.push_back(*seconds);
            }
        }
        return;
    }
    if (property == "transition-timing-function" || property == "animation-timing-function") {
        std::vector<CssEasing>& dest =
                property == "transition-timing-function" ? style.transition_easings : style.animation_easings;
        dest.clear();
        for (const std::string_view part : split_comma(value)) {
            if (const auto easing = parse_easing(part)) {
                dest.push_back(*easing);
            }
        }
        return;
    }
    if (property == "animation") {
        clear_animations(style);
        if (lower_copy(trim(value)) == "none") {
            return;
        }
        for (const std::string_view part : split_comma(value)) {
            const auto item = parse_animation_item(part);
            if (!item || item->name == "none") {
                if (item && item->name == "none") {
                    clear_animations(style);
                }
                continue;
            }
            style.animation_names.push_back(item->name);
            style.animation_durations.push_back(item->duration);
            style.animation_delays.push_back(item->delay);
            style.animation_easings.push_back(item->easing);
            style.animation_iterations.push_back(item->iterations);
        }
        return;
    }
    if (property == "animation-name") {
        style.animation_names.clear();
        for (const std::string& name : idents_of(value)) {
            if (name == "none") {
                style.animation_names.clear();
                return;
            }
            style.animation_names.push_back(name);
        }
        return;
    }
    if (property == "animation-iteration-count") {
        style.animation_iterations.clear();
        for (const std::string_view part : split_comma(value)) {
            const std::string name = lower_copy(trim(part));
            if (name == "infinite") {
                style.animation_iterations.push_back(-1.0f);
                continue;
            }
            if (const auto seconds = parse_time(name)) {
                // parse_time accepts a bare number as seconds; iteration-count is that number.
                style.animation_iterations.push_back(*seconds);
            }
        }
    }
}

void advance_motion(
        Element& element, const ComputedStyle& target, const Stylesheet* sheet, float dt, glm::vec2 basis) {
    element.motion_shown.clear();
    element.layout_inputs_changed = false;
    element.height_motion_active = false;

    std::vector<AnimationRuntime> players;
    players.reserve(target.animation_names.size());
    for (std::size_t i = 0; i < target.animation_names.size(); ++i) {
        AnimationRuntime player;
        player.name = target.animation_names[i];
        if (player.name.empty() || player.name == "none") {
            continue;
        }
        player.duration = list_last(target.animation_durations, i, 0.0f);
        player.delay = list_last(target.animation_delays, i, 0.0f);
        player.easing = list_last(target.animation_easings, i, CssEasing::Linear);
        player.iterations = list_last(target.animation_iterations, i, 1.0f);
        for (const AnimationRuntime& old : element.animation_players) {
            if (old.name == player.name) {
                player.elapsed = old.elapsed;
                break;
            }
        }
        const Keyframes* frames = sheet != nullptr ? find_keyframes(*sheet, player.name) : nullptr;
        if (frames != nullptr) {
            player.elapsed += dt;
            if (player.iterations >= 0.0f) {
                const float end = player.delay + player.duration * std::max(player.iterations, 0.0f);
                if (player.elapsed > end) {
                    player.elapsed = end;
                }
            }
            const float progress = animation_progress(player);
            const bool running = animation_running(player);
            if (progress >= 0.0f) {
                const float eased = apply_easing(player.easing, progress);
                for (const MotionProp prop : keyframe_props(*frames)) {
                    if (const auto sample = sample_prop(*frames, prop, eased, basis)) {
                        upsert_shown(element.motion_shown, prop, *sample, running);
                        if (running && affects_row_box(prop)) {
                            element.height_motion_active = true;
                        }
                    }
                }
            }
        }
        players.push_back(std::move(player));
    }
    element.animation_players = std::move(players);

    std::vector<TransitionRuntime> next_transitions;
    for (const TransitionSpec& spec : transition_specs(target)) {
        if (shown_has(element.motion_shown, spec.prop)) {
            continue;
        }
        TransitionRuntime player;
        player.prop = spec.prop;
        for (const TransitionRuntime& old : element.transition_players) {
            if (old.prop == spec.prop) {
                player = old;
                break;
            }
        }
        const MotionValue goal = read_prop(target, spec.prop, basis);
        if (!player.seen) {
            player.seen = true;
            player.shown = goal;
            player.to = goal;
            player.running = false;
        } else if (!player.running && same_spec(spec.prop, player.shown, goal)) {
            player.to = goal;
            player.shown = goal;
        } else {
            if (!player.running || !same_spec(spec.prop, player.to, goal)) {
                const MotionValue current = player.running
                        ? lerp_value(spec.prop, player.from, player.to, transition_u(player))
                        : player.shown;
                player.from = current;
                player.to = goal;
                player.elapsed = 0.0f;
                player.duration = spec.duration;
                player.delay = spec.delay;
                player.easing = spec.easing;
                player.running = true;
            } else {
                player.to = goal;
            }
            player.elapsed += dt;
            const float end = player.delay + std::max(player.duration, 0.0f);
            if (player.elapsed < player.delay) {
                player.shown = player.from;
            } else if (player.duration <= 0.0f || player.elapsed >= end) {
                player.shown = player.to;
                player.running = false;
                player.elapsed = end;
            } else {
                player.shown = lerp_value(spec.prop, player.from, player.to, transition_u(player));
            }
        }
        if (player.running && affects_row_box(spec.prop)) {
            element.height_motion_active = true;
        }
        if (affects_layout(spec.prop) && layout_differs(element, spec.prop, player.shown)) {
            element.layout_inputs_changed = true;
        }
        upsert_shown(element.motion_shown, spec.prop, player.shown, player.running);
        next_transitions.push_back(std::move(player));
    }
    element.transition_players = std::move(next_transitions);

    for (const ShownMotion& shown : element.motion_shown) {
        if (affects_layout(shown.prop) && layout_differs(element, shown.prop, shown.value)) {
            element.layout_inputs_changed = true;
        }
    }
}

void apply_motion_shown(const Element& element, ComputedStyle& style) {
    for (const ShownMotion& shown : element.motion_shown) {
        write_style(style, shown.prop, shown.value);
    }
}

void commit_motion_layout(Element& element) {
    for (const ShownMotion& shown : element.motion_shown) {
        if (affects_layout(shown.prop)) {
            write_element(element, shown.prop, shown.value);
        }
    }
}

void commit_motion_visuals(Element& element) {
    for (const ShownMotion& shown : element.motion_shown) {
        if (!affects_layout(shown.prop)) {
            write_element(element, shown.prop, shown.value);
        }
    }
}

}  // namespace engine::ui
