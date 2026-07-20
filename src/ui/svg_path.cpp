#include "svg_internal.h"

#include <algorithm>

namespace engine::ui::svg_detail {

void add_segment(
        std::vector<PathSegment>& segments, PathSegment::Kind kind, glm::vec2 p, glm::vec2 c1, glm::vec2 c2) {
    segments.push_back(PathSegment{kind, p, c1, c2});
}

void add_ellipse(std::vector<PathSegment>& segments, float cx, float cy, float rx, float ry) {
    add_segment(segments, PathSegment::Kind::Move, {cx + rx, cy});
    add_segment(
            segments, PathSegment::Kind::Cubic, {cx, cy + ry}, {cx + rx, cy + kCircle * ry},
            {cx + kCircle * rx, cy + ry});
    add_segment(
            segments, PathSegment::Kind::Cubic, {cx - rx, cy}, {cx - kCircle * rx, cy + ry},
            {cx - rx, cy + kCircle * ry});
    add_segment(
            segments, PathSegment::Kind::Cubic, {cx, cy - ry}, {cx - rx, cy - kCircle * ry},
            {cx - kCircle * rx, cy - ry});
    add_segment(
            segments, PathSegment::Kind::Cubic, {cx + rx, cy}, {cx + kCircle * rx, cy - ry},
            {cx + rx, cy - kCircle * ry});
    add_segment(segments, PathSegment::Kind::Close, {});
}

void add_rounded_rect(std::vector<PathSegment>& segments, float x, float y, float w, float h, float rx, float ry) {
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
    add_segment(
            segments, PathSegment::Kind::Cubic, {right, y + ry}, {right - rx + kCircle * rx, y},
            {right, y + ry - kCircle * ry});
    add_segment(segments, PathSegment::Kind::Line, {right, bottom - ry});
    add_segment(
            segments, PathSegment::Kind::Cubic, {right - rx, bottom}, {right, bottom - ry + kCircle * ry},
            {right - rx + kCircle * rx, bottom});
    add_segment(segments, PathSegment::Kind::Line, {x + rx, bottom});
    add_segment(
            segments, PathSegment::Kind::Cubic, {x, bottom - ry}, {x + rx - kCircle * rx, bottom},
            {x, bottom - ry + kCircle * ry});
    add_segment(segments, PathSegment::Kind::Line, {x, y + ry});
    add_segment(
            segments, PathSegment::Kind::Cubic, {x + rx, y}, {x, y + ry - kCircle * ry}, {x + rx - kCircle * rx, y});
    add_segment(segments, PathSegment::Kind::Close, {});
}

namespace {

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

void add_arc(
        std::vector<PathSegment>& segments, glm::vec2 from, float rx, float ry, float rotation_deg, int large,
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

void move_to(PathState& state, std::vector<PathSegment>& segments, glm::vec2 p) {
    state.current = p;
    state.start = p;
    state.moved = true;
    state.smooth_cubic = false;
    state.smooth_quad = false;
    add_segment(segments, PathSegment::Kind::Move, p);
}

void line_to(PathState& state, std::vector<PathSegment>& segments, glm::vec2 p) {
    state.current = p;
    state.smooth_cubic = false;
    state.smooth_quad = false;
    add_segment(segments, PathSegment::Kind::Line, p);
}

}

bool parse_path_data(std::string_view data, std::vector<PathSegment>& segments) {
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

bool parse_points(std::string_view text, std::vector<glm::vec2>& points) {
    Numbers numbers{text};
    while (numbers.has()) {
        const float x = numbers.number();
        const float y = numbers.number();
        points.push_back({x, y});
    }
    numbers.skip();
    return numbers.ok && numbers.i == numbers.text.size() && !points.empty();
}

void bake(std::vector<PathSegment>& segments, Aff2 xform) {
    for (PathSegment& segment : segments) {
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

}
