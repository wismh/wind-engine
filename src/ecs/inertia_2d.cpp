#include "inertia_2d.h"

#include <cmath>

namespace engine {
namespace {

[[nodiscard]] bool finite_xy(glm::vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

}

float inertia_2d_box(float mass, float width, float height) {
    if (!(mass > 0.f) || !std::isfinite(mass) || !std::isfinite(width) || !std::isfinite(height)) {
        return 0.f;
    }
    const float inertia = mass * (width * width + height * height) / 12.f;
    return inertia > 0.f && std::isfinite(inertia) ? inertia : 0.f;
}

float inertia_2d_circle(float mass, float radius) {
    if (!(mass > 0.f) || !std::isfinite(mass) || !std::isfinite(radius) || radius < 0.f) {
        return 0.f;
    }
    const float inertia = 0.5f * mass * radius * radius;
    return inertia > 0.f && std::isfinite(inertia) ? inertia : 0.f;
}

float inertia_2d_polygon(float mass, const glm::vec2* vertices, int count) {
    if (!(mass > 0.f) || !std::isfinite(mass) || vertices == nullptr || count < 3 || count > 8) {
        return 0.f;
    }
    float sum_d = 0.f;
    float sum_moment = 0.f;
    for (int i = 0; i < count; ++i) {
        const glm::vec2 v0 = vertices[i];
        const glm::vec2 v1 = vertices[(i + 1) % count];
        const glm::vec2 v2 = vertices[(i + 2) % count];
        if (!finite_xy(v0) || !finite_xy(v1)) {
            return 0.f;
        }
        const glm::vec2 edge0 = v1 - v0;
        const glm::vec2 edge1 = v2 - v1;
        if (edge0.x == 0.f && edge0.y == 0.f) {
            return 0.f;
        }
        const float turn = edge0.x * edge1.y - edge0.y * edge1.x;
        if (turn < 0.f) {
            return 0.f;
        }
        const float cross = v0.x * v1.y - v0.y * v1.x;
        const float int_x = v0.x * v0.x + v0.x * v1.x + v1.x * v1.x;
        const float int_y = v0.y * v0.y + v0.y * v1.y + v1.y * v1.y;
        sum_d += cross;
        sum_moment += cross * (int_x + int_y);
    }
    if (!(sum_d > 0.f)) {
        return 0.f;
    }
    const float inertia = mass * sum_moment / (6.f * sum_d);
    return inertia > 0.f && std::isfinite(inertia) ? inertia : 0.f;
}

}
