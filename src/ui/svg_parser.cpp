#include "svg_parser.h"

#include <tinyxml2.h>

#include <glm/vec2.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::ui {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kCircle = 0.55228474983f;
constexpr int kMaxDepth = 64;

struct Aff2 {
    float a = 1.0f;
    float b = 0.0f;
    float c = 0.0f;
    float d = 1.0f;
    float e = 0.0f;
    float f = 0.0f;

    [[nodiscard]] glm::vec2 apply(glm::vec2 p) const {
        return {a * p.x + c * p.y + e, b * p.x + d * p.y + f};
    }
};

Aff2 mul(Aff2 m, Aff2 n) {
    Aff2 o;
    o.a = m.a * n.a + m.c * n.b;
    o.b = m.b * n.a + m.d * n.b;
    o.c = m.a * n.c + m.c * n.d;
    o.d = m.b * n.c + m.d * n.d;
    o.e = m.a * n.e + m.c * n.f + m.e;
    o.f = m.b * n.e + m.d * n.f + m.f;
    return o;
}

float average_scale(Aff2 m) {
    const float sx = std::hypot(m.a, m.b);
    const float sy = std::hypot(m.c, m.d);
    return (sx + sy) * 0.5f;
}

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
    for (char &c: out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

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
        float value = 0.0f;
        const auto result = std::from_chars(text.data() + begin, text.data() + i, value);
        if (result.ec != std::errc{}) {
            ok = false;
            return 0.0f;
        }
        return value;
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

std::optional<float> length_value(std::string_view raw) {
    raw = trim(raw);
    if (raw.size() >= 2) {
        const std::string tail = lower_copy(raw.substr(raw.size() - 2));
        if (tail == "px") {
            raw = trim(raw.substr(0, raw.size() - 2));
        } else if (tail == "em" || tail == "ex" || tail == "pt" || tail == "pc" || tail == "in" || tail == "cm" ||
                   tail == "mm") {
            return std::nullopt;
        }
    }
    if (!raw.empty() && raw.back() == '%') {
        return std::nullopt;
    }
    Numbers numbers{raw};
    if (!numbers.has()) {
        return std::nullopt;
    }
    const float value = numbers.number();
    numbers.skip();
    if (!numbers.ok || numbers.i != numbers.text.size()) {
        return std::nullopt;
    }
    return value;
}

enum class ColorKind : std::uint8_t { None, Current, Literal };

struct PaintColor {
    ColorKind kind = ColorKind::Literal;
    glm::vec4 color{0.0f, 0.0f, 0.0f, 1.0f};
};

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

struct Style {
    PaintColor fill{};
    PaintColor stroke{ColorKind::None, {}};
    float stroke_width = 1.0f;
    float opacity = 1.0f;
    float fill_opacity = 1.0f;
    float stroke_opacity = 1.0f;
    bool skip = false;
};

bool dashed(std::string_view raw) {
    raw = trim(raw);
    const std::string key = lower_copy(raw);
    return !key.empty() && key != "none";
}

bool apply_decl(Style &style, std::string_view name, std::string_view value) {
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

bool apply_style_attr(Style &style, std::string_view text) {
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

bool apply_presentation(Style &style, const tinyxml2::XMLElement *element) {
    // `opacity` is not inherited: the parent contributes a multiplier, and this element's own value replaces
    // only its local factor. The style attribute is applied after the attributes, so it wins.
    const float inherited_opacity = style.opacity;
    style.opacity = 1.0f;
    auto take = [&](const char *name) {
        if (const char *value = element->Attribute(name)) {
            return apply_decl(style, name, value);
        }
        return true;
    };
    if (!take("fill") || !take("stroke") || !take("stroke-width") || !take("opacity") || !take("fill-opacity") ||
        !take("stroke-opacity") || !take("fill-rule") || !take("stroke-dasharray") || !take("display") ||
        !take("visibility")) {
        return false;
    }
    if (const char *inline_style = element->Attribute("style")) {
        if (!apply_style_attr(style, inline_style)) {
            return false;
        }
    }
    style.opacity *= inherited_opacity;
    return true;
}

bool parse_transform(std::string_view text, Aff2 &out) {
    Numbers numbers{text};
    Aff2 combined;
    numbers.skip();
    while (numbers.i < numbers.text.size()) {
        const std::size_t begin = numbers.i;
        while (numbers.i < numbers.text.size() &&
               std::isalpha(static_cast<unsigned char>(numbers.text[numbers.i])) != 0) {
            ++numbers.i;
        }
        const std::string_view name = numbers.text.substr(begin, numbers.i - begin);
        numbers.skip();
        if (numbers.i >= numbers.text.size() || numbers.text[numbers.i] != '(') {
            return false;
        }
        ++numbers.i;
        float args[6] = {};
        int count = 0;
        while (count < 6 && numbers.has()) {
            args[count++] = numbers.number();
        }
        numbers.skip();
        if (!numbers.ok || numbers.i >= numbers.text.size() || numbers.text[numbers.i] != ')') {
            return false;
        }
        ++numbers.i;
        Aff2 local;
        if (name == "matrix") {
            if (count != 6) {
                return false;
            }
            local = {args[0], args[1], args[2], args[3], args[4], args[5]};
        } else if (name == "translate") {
            if (count < 1 || count > 2) {
                return false;
            }
            local.e = args[0];
            local.f = count == 2 ? args[1] : 0.0f;
        } else if (name == "scale") {
            if (count < 1 || count > 2) {
                return false;
            }
            local.a = args[0];
            local.d = count == 2 ? args[1] : args[0];
        } else if (name == "rotate") {
            if (count != 1 && count != 3) {
                return false;
            }
            const float rad = args[0] * kPi / 180.0f;
            const float cos_a = std::cos(rad);
            const float sin_a = std::sin(rad);
            local.a = cos_a;
            local.b = sin_a;
            local.c = -sin_a;
            local.d = cos_a;
            if (count == 3) {
                Aff2 to;
                to.e = args[1];
                to.f = args[2];
                Aff2 back;
                back.e = -args[1];
                back.f = -args[2];
                local = mul(mul(to, local), back);
            }
        } else {
            return false;
        }
        combined = mul(combined, local);
        numbers.skip();
    }
    if (!numbers.ok) {
        return false;
    }
    out = combined;
    return true;
}

bool element_transform(const tinyxml2::XMLElement *element, Aff2 parent, Aff2 &out) {
    Aff2 local;
    if (const char *value = element->Attribute("transform")) {
        if (!parse_transform(value, local)) {
            return false;
        }
    }
    out = mul(parent, local);
    return true;
}

float attr_length(const tinyxml2::XMLElement *element, const char *name, float fallback, bool &ok) {
    const char *value = element->Attribute(name);
    if (value == nullptr) {
        return fallback;
    }
    const auto parsed = length_value(value);
    if (!parsed) {
        ok = false;
        return fallback;
    }
    return *parsed;
}

void add_segment(std::vector<PathSegment> &segments, PathSegment::Kind kind, glm::vec2 p, glm::vec2 c1 = {},
                 glm::vec2 c2 = {}) {
    segments.push_back(PathSegment{kind, p, c1, c2});
}

void add_ellipse(std::vector<PathSegment> &segments, float cx, float cy, float rx, float ry) {
    add_segment(segments, PathSegment::Kind::Move, {cx + rx, cy});
    add_segment(segments, PathSegment::Kind::Cubic, {cx, cy + ry}, {cx + rx, cy + kCircle * ry},
                {cx + kCircle * rx, cy + ry});
    add_segment(segments, PathSegment::Kind::Cubic, {cx - rx, cy}, {cx - kCircle * rx, cy + ry},
                {cx - rx, cy + kCircle * ry});
    add_segment(segments, PathSegment::Kind::Cubic, {cx, cy - ry}, {cx - rx, cy - kCircle * ry},
                {cx - kCircle * rx, cy - ry});
    add_segment(segments, PathSegment::Kind::Cubic, {cx + rx, cy}, {cx + kCircle * rx, cy - ry},
                {cx + rx, cy - kCircle * ry});
    add_segment(segments, PathSegment::Kind::Close, {});
}

void add_rounded_rect(std::vector<PathSegment> &segments, float x, float y, float w, float h, float rx, float ry) {
    rx = std::min(std::abs(rx), w * 0.5f);
    ry = std::min(std::abs(ry), h * 0.5f);
    if (rx <= 0.0f && ry <= 0.0f) {
        add_segment(segments, PathSegment::Kind::Move, {x, y});
        add_segment(segments, PathSegment::Kind::Line, {x + w, y});
        add_segment(segments, PathSegment::Kind::Line, {x + w, y + h});
        add_segment(segments, PathSegment::Kind::Line, {x, y + h});
        add_segment(segments, PathSegment::Kind::Close, {});
        return;
    }
    const float right = x + w;
    const float bottom = y + h;
    add_segment(segments, PathSegment::Kind::Move, {x + rx, y});
    add_segment(segments, PathSegment::Kind::Line, {right - rx, y});
    add_segment(segments, PathSegment::Kind::Cubic, {right, y + ry}, {right - rx + kCircle * rx, y},
                {right, y + ry - kCircle * ry});
    add_segment(segments, PathSegment::Kind::Line, {right, bottom - ry});
    add_segment(segments, PathSegment::Kind::Cubic, {right - rx, bottom}, {right, bottom - ry + kCircle * ry},
                {right - rx + kCircle * rx, bottom});
    add_segment(segments, PathSegment::Kind::Line, {x + rx, bottom});
    add_segment(segments, PathSegment::Kind::Cubic, {x, bottom - ry}, {x + rx - kCircle * rx, bottom},
                {x, bottom - ry + kCircle * ry});
    add_segment(segments, PathSegment::Kind::Line, {x, y + ry});
    add_segment(segments, PathSegment::Kind::Cubic, {x + rx, y}, {x, y + ry - kCircle * ry},
                {x + rx - kCircle * rx, y});
    add_segment(segments, PathSegment::Kind::Close, {});
}

float vec_angle(glm::vec2 u, glm::vec2 v) {
    const float dot = u.x * v.x + u.y * v.y;
    const float len = std::sqrt((u.x * u.x + u.y * u.y) * (v.x * v.x + v.y * v.y));
    const float cosine = len > 0.0f ? std::clamp(dot / len, -1.0f, 1.0f) : 1.0f;
    const float angle = std::acos(cosine);
    if (u.x * v.y - u.y * v.x < 0.0f) {
        return -angle;
    }
    return angle;
}

void add_arc(std::vector<PathSegment> &segments, glm::vec2 from, float rx, float ry, float rotation_deg, int large,
             int sweep, glm::vec2 to) {
    if (from == to) {
        return;
    }
    rx = std::abs(rx);
    ry = std::abs(ry);
    if (!(rx > 0.0f) || !(ry > 0.0f)) {
        add_segment(segments, PathSegment::Kind::Line, to);
        return;
    }
    const float phi = rotation_deg * kPi / 180.0f;
    const float cos_phi = std::cos(phi);
    const float sin_phi = std::sin(phi);
    const float dx = (from.x - to.x) * 0.5f;
    const float dy = (from.y - to.y) * 0.5f;
    const float x1p = cos_phi * dx + sin_phi * dy;
    const float y1p = -sin_phi * dx + cos_phi * dy;
    float rx_sq = rx * rx;
    float ry_sq = ry * ry;
    const float x1p_sq = x1p * x1p;
    const float y1p_sq = y1p * y1p;
    const float lambda = x1p_sq / rx_sq + y1p_sq / ry_sq;
    if (lambda > 1.0f) {
        const float grow = std::sqrt(lambda);
        rx *= grow;
        ry *= grow;
        rx_sq = rx * rx;
        ry_sq = ry * ry;
    }
    const float denom = rx_sq * y1p_sq + ry_sq * x1p_sq;
    float coef = 0.0f;
    if (denom > 0.0f) {
        const float sq = std::max(0.0f, (rx_sq * ry_sq - rx_sq * y1p_sq - ry_sq * x1p_sq) / denom);
        coef = std::sqrt(sq);
    }
    if (large == sweep) {
        coef = -coef;
    }
    const float cxp = coef * rx * y1p / ry;
    const float cyp = coef * -ry * x1p / rx;
    const float cx = cos_phi * cxp - sin_phi * cyp + (from.x + to.x) * 0.5f;
    const float cy = sin_phi * cxp + cos_phi * cyp + (from.y + to.y) * 0.5f;
    const glm::vec2 u{(x1p - cxp) / rx, (y1p - cyp) / ry};
    const glm::vec2 v{(-x1p - cxp) / rx, (-y1p - cyp) / ry};
    const float theta1 = vec_angle({1.0f, 0.0f}, u);
    float dtheta = vec_angle(u, v);
    if (sweep == 0 && dtheta > 0.0f) {
        dtheta -= 2.0f * kPi;
    } else if (sweep == 1 && dtheta < 0.0f) {
        dtheta += 2.0f * kPi;
    }
    const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(dtheta) / (kPi * 0.5f) - 1.0e-4f)));
    const float delta = dtheta / static_cast<float>(steps);
    auto place = [&](float theta) {
        const float ex = rx * std::cos(theta);
        const float ey = ry * std::sin(theta);
        return glm::vec2{cos_phi * ex - sin_phi * ey + cx, sin_phi * ex + cos_phi * ey + cy};
    };
    auto deriv = [&](float theta) {
        const float ex = -rx * std::sin(theta);
        const float ey = ry * std::cos(theta);
        return glm::vec2{cos_phi * ex - sin_phi * ey, sin_phi * ex + cos_phi * ey};
    };
    for (int step = 0; step < steps; ++step) {
        const float t0 = theta1 + delta * static_cast<float>(step);
        const float t1 = t0 + delta;
        const float k = 4.0f / 3.0f * std::tan(delta * 0.25f);
        const glm::vec2 p0 = place(t0);
        const glm::vec2 p1 = place(t1);
        add_segment(segments, PathSegment::Kind::Cubic, p1, p0 + deriv(t0) * k, p1 - deriv(t1) * k);
    }
}

struct PathState {
    glm::vec2 current{};
    glm::vec2 start{};
    glm::vec2 control{};
    bool smooth_cubic = false;
    bool smooth_quad = false;
    bool moved = false;
};

void move_to(PathState &state, std::vector<PathSegment> &segments, glm::vec2 p) {
    state.current = p;
    state.start = p;
    state.moved = true;
    state.smooth_cubic = false;
    state.smooth_quad = false;
    add_segment(segments, PathSegment::Kind::Move, p);
}

void line_to(PathState &state, std::vector<PathSegment> &segments, glm::vec2 p) {
    state.current = p;
    state.smooth_cubic = false;
    state.smooth_quad = false;
    add_segment(segments, PathSegment::Kind::Line, p);
}

bool parse_path_data(std::string_view data, std::vector<PathSegment> &segments) {
    Numbers numbers{data};
    PathState state;
    numbers.skip();
    while (numbers.i < numbers.text.size()) {
        const char command = numbers.text[numbers.i];
        if (std::isalpha(static_cast<unsigned char>(command)) == 0) {
            return false;
        }
        ++numbers.i;
        const bool relative = command >= 'a' && command <= 'z';
        const char op = static_cast<char>(std::tolower(static_cast<unsigned char>(command)));
        auto absolute = [&](float x, float y) {
            if (relative) {
                return state.current + glm::vec2{x, y};
            }
            return glm::vec2{x, y};
        };
        // Read the pair before combining it. Two `number()` calls in one argument list are unordered.
        auto read_point = [&]() {
            const float x = numbers.number();
            const float y = numbers.number();
            return absolute(x, y);
        };
        if (op == 'm') {
            bool first = true;
            if (!numbers.has()) {
                return false;
            }
            while (numbers.has()) {
                const float x = numbers.number();
                const float y = numbers.number();
                const glm::vec2 p = absolute(x, y);
                if (first) {
                    move_to(state, segments, p);
                    first = false;
                } else {
                    line_to(state, segments, p);
                }
            }
        } else if (op == 'l') {
            if (!numbers.has()) {
                return false;
            }
            while (numbers.has()) {
                line_to(state, segments, read_point());
            }
        } else if (op == 'h') {
            if (!numbers.has()) {
                return false;
            }
            while (numbers.has()) {
                const float x = numbers.number();
                line_to(state, segments, {relative ? state.current.x + x : x, state.current.y});
            }
        } else if (op == 'v') {
            if (!numbers.has()) {
                return false;
            }
            while (numbers.has()) {
                const float y = numbers.number();
                line_to(state, segments, {state.current.x, relative ? state.current.y + y : y});
            }
        } else if (op == 'c') {
            if (!numbers.has()) {
                return false;
            }
            while (numbers.has()) {
                const glm::vec2 c1 = read_point();
                const glm::vec2 c2 = read_point();
                const glm::vec2 p = read_point();
                add_segment(segments, PathSegment::Kind::Cubic, p, c1, c2);
                state.current = p;
                state.control = c2;
                state.smooth_cubic = true;
                state.smooth_quad = false;
            }
        } else if (op == 's') {
            if (!numbers.has()) {
                return false;
            }
            while (numbers.has()) {
                const glm::vec2 c1 = state.smooth_cubic ? state.current * 2.0f - state.control : state.current;
                const glm::vec2 c2 = read_point();
                const glm::vec2 p = read_point();
                add_segment(segments, PathSegment::Kind::Cubic, p, c1, c2);
                state.current = p;
                state.control = c2;
                state.smooth_cubic = true;
                state.smooth_quad = false;
            }
        } else if (op == 'q') {
            if (!numbers.has()) {
                return false;
            }
            while (numbers.has()) {
                const glm::vec2 c1 = read_point();
                const glm::vec2 p = read_point();
                add_segment(segments, PathSegment::Kind::Quad, p, c1);
                state.current = p;
                state.control = c1;
                state.smooth_quad = true;
                state.smooth_cubic = false;
            }
        } else if (op == 't') {
            if (!numbers.has()) {
                return false;
            }
            while (numbers.has()) {
                const glm::vec2 c1 = state.smooth_quad ? state.current * 2.0f - state.control : state.current;
                const glm::vec2 p = read_point();
                add_segment(segments, PathSegment::Kind::Quad, p, c1);
                state.current = p;
                state.control = c1;
                state.smooth_quad = true;
                state.smooth_cubic = false;
            }
        } else if (op == 'a') {
            if (!numbers.has()) {
                return false;
            }
            while (numbers.has()) {
                const float rx = numbers.number();
                const float ry = numbers.number();
                const float rotation = numbers.number();
                const int large = numbers.flag();
                const int sweep = numbers.flag();
                const glm::vec2 p = read_point();
                if (!numbers.ok) {
                    return false;
                }
                add_arc(segments, state.current, rx, ry, rotation, large, sweep, p);
                state.current = p;
                state.smooth_cubic = false;
                state.smooth_quad = false;
            }
        } else if (op == 'z') {
            if (state.moved) {
                add_segment(segments, PathSegment::Kind::Close, {});
                state.current = state.start;
            }
            state.smooth_cubic = false;
            state.smooth_quad = false;
        } else {
            return false;
        }
        if (!numbers.ok) {
            return false;
        }
        numbers.skip();
    }
    return numbers.ok;
}

bool parse_points(std::string_view text, std::vector<glm::vec2> &points) {
    Numbers numbers{text};
    while (numbers.has()) {
        const float x = numbers.number();
        const float y = numbers.number();
        points.push_back({x, y});
    }
    numbers.skip();
    return numbers.ok && numbers.i == numbers.text.size() && !points.empty();
}

void bake(std::vector<PathSegment> &segments, Aff2 xform) {
    for (PathSegment &segment: segments) {
        if (segment.kind == PathSegment::Kind::Close) {
            continue;
        }
        segment.p = xform.apply(segment.p);
        if (segment.kind == PathSegment::Kind::Quad || segment.kind == PathSegment::Kind::Cubic) {
            segment.c1 = xform.apply(segment.c1);
        }
        if (segment.kind == PathSegment::Kind::Cubic) {
            segment.c2 = xform.apply(segment.c2);
        }
    }
}

glm::vec4 painted(PaintColor color, float alpha) {
    return {color.color.r, color.color.g, color.color.b, color.color.a * alpha};
}

void emit(SvgPicture &picture, std::vector<PathSegment> segments, const Style &style, Aff2 xform) {
    if (segments.empty()) {
        return;
    }
    bake(segments, xform);
    const float width = style.stroke_width * average_scale(xform);
    if (style.fill.kind != ColorKind::None) {
        SvgShape shape;
        shape.segments = segments;
        shape.paint = SvgShape::Paint::Fill;
        shape.current_color = style.fill.kind == ColorKind::Current;
        shape.color = painted(style.fill, style.opacity * style.fill_opacity);
        picture.shapes.push_back(std::move(shape));
    }
    if (style.stroke.kind != ColorKind::None && width > 0.0f) {
        SvgShape shape;
        shape.segments = std::move(segments);
        shape.paint = SvgShape::Paint::Stroke;
        shape.width = width;
        shape.current_color = style.stroke.kind == ColorKind::Current;
        shape.color = painted(style.stroke, style.opacity * style.stroke_opacity);
        picture.shapes.push_back(std::move(shape));
    }
}

bool emit_shape(SvgPicture &picture, const tinyxml2::XMLElement *element, const Style &style, Aff2 xform) {
    const std::string_view tag = element->Name();
    std::vector<PathSegment> segments;
    bool ok = true;
    if (tag == "path") {
        const char *data = element->Attribute("d");
        if (data == nullptr || !parse_path_data(data, segments)) {
            return false;
        }
    } else if (tag == "rect") {
        const float x = attr_length(element, "x", 0.0f, ok);
        const float y = attr_length(element, "y", 0.0f, ok);
        const float w = attr_length(element, "width", 0.0f, ok);
        const float h = attr_length(element, "height", 0.0f, ok);
        float rx = attr_length(element, "rx", -1.0f, ok);
        float ry = attr_length(element, "ry", -1.0f, ok);
        if (!ok || !(w > 0.0f) || !(h > 0.0f)) {
            return ok;
        }
        if (rx < 0.0f && ry < 0.0f) {
            rx = 0.0f;
            ry = 0.0f;
        } else if (rx < 0.0f) {
            rx = ry;
        } else if (ry < 0.0f) {
            ry = rx;
        }
        add_rounded_rect(segments, x, y, w, h, rx, ry);
    } else if (tag == "circle" || tag == "ellipse") {
        const float cx = attr_length(element, "cx", 0.0f, ok);
        const float cy = attr_length(element, "cy", 0.0f, ok);
        const float rx = tag == "circle" ? attr_length(element, "r", 0.0f, ok) : attr_length(element, "rx", 0.0f, ok);
        const float ry = tag == "circle" ? rx : attr_length(element, "ry", 0.0f, ok);
        if (!ok || !(rx > 0.0f) || !(ry > 0.0f)) {
            return ok;
        }
        add_ellipse(segments, cx, cy, rx, ry);
    } else if (tag == "line") {
        const float x1 = attr_length(element, "x1", 0.0f, ok);
        const float y1 = attr_length(element, "y1", 0.0f, ok);
        const float x2 = attr_length(element, "x2", 0.0f, ok);
        const float y2 = attr_length(element, "y2", 0.0f, ok);
        if (!ok) {
            return false;
        }
        add_segment(segments, PathSegment::Kind::Move, {x1, y1});
        add_segment(segments, PathSegment::Kind::Line, {x2, y2});
    } else if (tag == "polyline" || tag == "polygon") {
        const char *value = element->Attribute("points");
        std::vector<glm::vec2> points;
        if (value == nullptr || !parse_points(value, points)) {
            return false;
        }
        add_segment(segments, PathSegment::Kind::Move, points.front());
        for (std::size_t i = 1; i < points.size(); ++i) {
            add_segment(segments, PathSegment::Kind::Line, points[i]);
        }
        if (tag == "polygon") {
            add_segment(segments, PathSegment::Kind::Close, {});
        }
    } else {
        return false;
    }
    emit(picture, std::move(segments), style, xform);
    return true;
}

bool forbidden(std::string_view tag) {
    return tag == "text" || tag == "tspan" || tag == "textPath" || tag == "image" || tag == "use" || tag == "style" ||
           tag == "linearGradient" || tag == "radialGradient" || tag == "filter" || tag == "clipPath" ||
           tag == "mask" || tag == "pattern" || tag == "symbol" || tag == "marker" || tag == "script" ||
           tag == "foreignObject" || tag == "animate" || tag == "animateTransform" || tag == "set" || tag == "switch";
}

bool walk(SvgPicture &picture, const tinyxml2::XMLElement *element, Style style, Aff2 xform, bool draw, int depth) {
    if (depth > kMaxDepth || element->Name() == nullptr) {
        return false;
    }
    const std::string_view tag = element->Name();
    if (tag == "title" || tag == "desc" || tag == "metadata") {
        return true;
    }
    if (forbidden(tag)) {
        return false;
    }
    if (tag == "defs") {
        for (const tinyxml2::XMLElement *child = element->FirstChildElement(); child != nullptr;
             child = child->NextSiblingElement()) {
            if (!walk(picture, child, style, xform, false, depth + 1)) {
                return false;
            }
        }
        return true;
    }
    if (tag == "g") {
        if (!apply_presentation(style, element) || !element_transform(element, xform, xform)) {
            return false;
        }
        if (style.skip) {
            return true;
        }
        for (const tinyxml2::XMLElement *child = element->FirstChildElement(); child != nullptr;
             child = child->NextSiblingElement()) {
            if (!walk(picture, child, style, xform, draw, depth + 1)) {
                return false;
            }
        }
        return true;
    }
    if (tag == "path" || tag == "rect" || tag == "circle" || tag == "ellipse" || tag == "line" || tag == "polyline" ||
        tag == "polygon") {
        if (!apply_presentation(style, element) || !element_transform(element, xform, xform)) {
            return false;
        }
        if (!draw || style.skip) {
            return true;
        }
        return emit_shape(picture, element, style, xform);
    }
    return false;
}

std::optional<SvgPicture> parse_root(const tinyxml2::XMLElement *root) {
    if (root == nullptr || std::string_view{root->Name()} != "svg") {
        return std::nullopt;
    }
    const char *box = root->Attribute("viewBox");
    if (box == nullptr) {
        return std::nullopt;
    }
    Numbers numbers{box};
    const float min_x = numbers.number();
    const float min_y = numbers.number();
    const float width = numbers.number();
    const float height = numbers.number();
    numbers.skip();
    if (!numbers.ok || numbers.i != numbers.text.size() || !(width > 0.0f) || !(height > 0.0f)) {
        return std::nullopt;
    }
    Style style;
    Aff2 xform;
    if (!apply_presentation(style, root) || !element_transform(root, Aff2{}, xform)) {
        return std::nullopt;
    }
    SvgPicture picture;
    picture.min_x = min_x;
    picture.min_y = min_y;
    picture.width = width;
    picture.height = height;
    if (style.skip) {
        return picture;
    }
    for (const tinyxml2::XMLElement *child = root->FirstChildElement(); child != nullptr;
         child = child->NextSiblingElement()) {
        if (!walk(picture, child, style, xform, true, 1)) {
            return std::nullopt;
        }
    }
    return picture;
}

}

std::optional<SvgPicture> parse_svg(std::string_view xml) {
    tinyxml2::XMLDocument document;
    if (document.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) {
        return std::nullopt;
    }
    return parse_root(document.RootElement());
}

}
