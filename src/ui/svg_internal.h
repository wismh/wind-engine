#pragma once

// Pieces of the SVG reader shared by its source files. Not a public header: `parse_svg` in svg_parser.h is the entry.

#include <engine/core/parse_float.h>
#include <engine/ui/path.h>

#include <tinyxml2.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::ui::svg_detail {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kCircle = 0.55228474983f;

struct Aff2 {
    float a = 1.0f;
    float b = 0.0f;
    float c = 0.0f;
    float d = 1.0f;
    float e = 0.0f;
    float f = 0.0f;

    [[nodiscard]] glm::vec2 apply(glm::vec2 p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
};

inline Aff2 mul(Aff2 m, Aff2 n) {
    Aff2 o;
    o.a = m.a * n.a + m.c * n.b;
    o.b = m.b * n.a + m.d * n.b;
    o.c = m.a * n.c + m.c * n.d;
    o.d = m.b * n.c + m.d * n.d;
    o.e = m.a * n.e + m.c * n.f + m.e;
    o.f = m.b * n.e + m.d * n.f + m.f;
    return o;
}

inline float average_scale(Aff2 m) {
    const float sx = std::hypot(m.a, m.b);
    const float sy = std::hypot(m.c, m.d);
    return (sx + sy) * 0.5f;
}


// Whitespace trimmed from both ends.
[[nodiscard]] std::string_view trim(std::string_view value);
[[nodiscard]] std::string lower_copy(std::string_view value);

struct Numbers {
    std::string_view text;
    std::size_t i = 0;
    bool ok = true;

    void skip() {
        while (i < text.size()) {
            const char c = text[i];
            if (std::isspace(static_cast<unsigned char>(c)) != 0 || c == ',') {
                ++i;
                continue;
            }
            break;
        }
    }

    [[nodiscard]] bool has() {
        skip();
        if (i >= text.size()) {
            return false;
        }
        const char c = text[i];
        return c == '+' || c == '-' || c == '.' || std::isdigit(static_cast<unsigned char>(c)) != 0;
    }

    float number() {
        skip();
        const std::size_t begin = i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
            ++i;
        }
        bool dot = false;
        bool digit = false;
        while (i < text.size()) {
            const char c = text[i];
            if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
                digit = true;
                ++i;
                continue;
            }
            if (c == '.' && !dot) {
                dot = true;
                ++i;
                continue;
            }
            break;
        }
        if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
            std::size_t exp = i + 1;
            if (exp < text.size() && (text[exp] == '+' || text[exp] == '-')) {
                ++exp;
            }
            if (exp < text.size() && std::isdigit(static_cast<unsigned char>(text[exp])) != 0) {
                i = exp + 1;
                while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
                    ++i;
                }
            }
        }
        if (!digit) {
            ok = false;
            return 0.0f;
        }
        double value = 0.0;
        if (!parse_float(text.substr(begin, i - begin), value)) {
            ok = false;
            return 0.0f;
        }
        return static_cast<float>(value);
    }

    // An arc flag is one character, so `01` is two flags and not the number 1.
    int flag() {
        skip();
        if (i >= text.size() || (text[i] != '0' && text[i] != '1')) {
            ok = false;
            return 0;
        }
        const int value = text[i] == '1' ? 1 : 0;
        ++i;
        return value;
    }
};

// A length in user units. Absolute units other than px and percentages are rejected (nullopt).
[[nodiscard]] std::optional<float> length_value(std::string_view raw);

enum class ColorKind : std::uint8_t { None, Current, Literal };

struct PaintColor {
    ColorKind kind = ColorKind::Literal;
    glm::vec4 color{0.0f, 0.0f, 0.0f, 1.0f};
};

[[nodiscard]] std::optional<PaintColor> parse_color(std::string_view raw);

struct Style {
    PaintColor fill{};
    PaintColor stroke{ColorKind::None, {}};
    float stroke_width = 1.0f;
    float opacity = 1.0f;
    float fill_opacity = 1.0f;
    float stroke_opacity = 1.0f;
    bool skip = false;
};

// Applies the presentation attributes and the `style` attribute of `element` on top of the inherited `style`. False
// when one of them is something the painter cannot draw.
[[nodiscard]] bool apply_presentation(Style& style, const tinyxml2::XMLElement* element);

[[nodiscard]] bool parse_transform(std::string_view text, Aff2& out);
[[nodiscard]] bool element_transform(const tinyxml2::XMLElement* element, Aff2 parent, Aff2& out);
[[nodiscard]] float attr_length(const tinyxml2::XMLElement* element, const char* name, float fallback, bool& ok);

void add_segment(std::vector<PathSegment>& segments, PathSegment::Kind kind, glm::vec2 p, glm::vec2 c1 = {},
        glm::vec2 c2 = {});
void add_ellipse(std::vector<PathSegment>& segments, float cx, float cy, float rx, float ry);
void add_rounded_rect(std::vector<PathSegment>& segments, float x, float y, float w, float h, float rx, float ry);
[[nodiscard]] bool parse_path_data(std::string_view data, std::vector<PathSegment>& segments);
[[nodiscard]] bool parse_points(std::string_view text, std::vector<glm::vec2>& points);
// Applies `xform` to every point of `segments`.
void bake(std::vector<PathSegment>& segments, Aff2 xform);

}
