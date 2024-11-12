#pragma once

#include <glm/common.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <type_traits>
#include <utility>
#include <vector>

namespace engine::render {

enum class KeyInterpolation {
    Linear,
    Smooth, // cubic Hermite / smoothstep
    Step,   // constant until next key
};

template<typename T>
struct CurveKey {
    float time = 0.0f;
    T value{};
    KeyInterpolation interpolation = KeyInterpolation::Linear;

    constexpr bool operator==(const CurveKey&) const noexcept = default;
};

namespace detail {

template<typename ValT>
[[nodiscard]] ValT interpolate_curve_value(const ValT& a, const ValT& b, float factor) {
    if constexpr (std::is_arithmetic_v<ValT>) {
        return static_cast<ValT>(a + (b - a) * factor);
    } else {
        return glm::mix(a, b, factor);
    }
}

}

template<typename T>
class Curve {
public:
    std::vector<CurveKey<T>> keys;

    Curve() = default;

    Curve(std::initializer_list<CurveKey<T>> init_keys) : keys(init_keys) {
        sort_keys();
    }

    explicit Curve(std::vector<CurveKey<T>> init_keys) : keys(std::move(init_keys)) {
        sort_keys();
    }

    [[nodiscard]] bool empty() const noexcept {
        return keys.empty();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return keys.size();
    }

    void clear() noexcept {
        keys.clear();
    }

    void add_key(float time, const T& value, KeyInterpolation interp = KeyInterpolation::Linear) {
        keys.push_back(CurveKey<T>{time, value, interp});
        sort_keys();
    }

    [[nodiscard]] T evaluate(float t) const {
        if (keys.empty()) {
            return T{};
        }
        if (keys.size() == 1 || t <= keys.front().time) {
            return keys.front().value;
        }
        if (t >= keys.back().time) {
            return keys.back().value;
        }

        for (std::size_t i = 1; i < keys.size(); ++i) {
            if (t < keys[i].time) {
                const auto& k0 = keys[i - 1];
                const auto& k1 = keys[i];
                const float dt = k1.time - k0.time;
                if (dt <= 1e-6f) {
                    return k1.value;
                }

                float factor = (t - k0.time) / dt;
                switch (k0.interpolation) {
                    case KeyInterpolation::Step:
                        factor = 0.0f;
                        break;
                    case KeyInterpolation::Smooth:
                        factor = factor * factor * (3.0f - 2.0f * factor);
                        break;
                    case KeyInterpolation::Linear:
                    default:
                        break;
                }

                return detail::interpolate_curve_value(k0.value, k1.value, factor);
            }
            if (t == keys[i].time) {
                return keys[i].value;
            }
        }

        return keys.back().value;
    }

    static Curve<T> linear(const T& start, const T& end) {
        return Curve<T>{{0.0f, start, KeyInterpolation::Linear}, {1.0f, end, KeyInterpolation::Linear}};
    }

    static Curve<T> constant(const T& val) {
        return Curve<T>{{0.0f, val, KeyInterpolation::Linear}, {1.0f, val, KeyInterpolation::Linear}};
    }

private:
    void sort_keys() {
        std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) {
            return a.time < b.time;
        });
    }
};

}
