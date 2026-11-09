#include <engine/core/det_math.h>

#include <cmath>
#include <cstdint>
#include <limits>

// Only + - * / and exact operations (floor, fmod, comparisons) in IEEE double, evaluated in source order: the engine
// builds with contraction off (docs/tech/build/CMake.md), so no step becomes a fused multiply-add on any target.

namespace engine {
namespace {

inline constexpr double kPi = 3.14159265358979311600e+00;
inline constexpr double kHalfPi = 1.57079632679489655800e+00;
inline constexpr double kTwoOverPi = 6.36619772367581382433e-01;
inline constexpr double kTwoPi = 6.28318530717958623200e+00;

// pi/2 in three parts (fdlibm's pio2_1, pio2_2, pio2_3 and the tail of the third). The first two have 33 significant
// bits, so k * part is exact for |k| < 2^20, the range below `kReduceLimit`.
inline constexpr double kPio2Part1 = 1.57079632673412561417e+00;
inline constexpr double kPio2Part2 = 6.07710050630396597660e-11;
inline constexpr double kPio2Part3 = 2.02226624871116645580e-21;
inline constexpr double kPio2Part3Tail = 8.47842766036889956997e-32;
// Beyond this, the argument is first folded by fmod (exact) with the double 2 pi. Not the true reduction, but the same
// on every platform, and no simulation angle gets near it.
inline constexpr double kReduceLimit = 1.0e6;

inline constexpr double kSqrt3 = 1.73205080756887719318e+00;
inline constexpr double kTwoMinusSqrt3 = 2.67949192431122695e-01;
inline constexpr double kPiOver6 = 5.23598775598298815659e-01;
inline constexpr double kPiOver4 = 7.85398163397448278999e-01;

// sin and cos on |r| <= pi/4 by Taylor series in Horner form. The first omitted terms are below 1e-19.
[[nodiscard]] double sin_poly(double r) noexcept {
    const double z = r * r;
    double p = 1.0 / 355687428096000.0;  // 1/17!
    p = p * z - 1.0 / 1307674368000.0;   // 1/15!
    p = p * z + 1.0 / 6227020800.0;      // 1/13!
    p = p * z - 1.0 / 39916800.0;        // 1/11!
    p = p * z + 1.0 / 362880.0;          // 1/9!
    p = p * z - 1.0 / 5040.0;            // 1/7!
    p = p * z + 1.0 / 120.0;             // 1/5!
    p = p * z - 1.0 / 6.0;               // 1/3!
    return r + r * z * p;
}

[[nodiscard]] double cos_poly(double r) noexcept {
    const double z = r * r;
    double p = 1.0 / 6402373705728000.0;  // 1/18!
    p = p * z - 1.0 / 20922789888000.0;   // 1/16!
    p = p * z + 1.0 / 87178291200.0;      // 1/14!
    p = p * z - 1.0 / 479001600.0;        // 1/12!
    p = p * z + 1.0 / 3628800.0;          // 1/10!
    p = p * z - 1.0 / 40320.0;            // 1/8!
    p = p * z + 1.0 / 720.0;              // 1/6!
    p = p * z - 1.0 / 24.0;               // 1/4!
    p = p * z + 0.5;                      // 1/2!
    return 1.0 - z * p;
}

// atan on |u| <= 2 - sqrt(3) (u^2 < 0.072) by Taylor series. The first omitted term is below 1e-17.
[[nodiscard]] double atan_poly(double u) noexcept {
    const double z = u * u;
    double p = -1.0 / 27.0;
    p = p * z + 1.0 / 25.0;
    p = p * z - 1.0 / 23.0;
    p = p * z + 1.0 / 21.0;
    p = p * z - 1.0 / 19.0;
    p = p * z + 1.0 / 17.0;
    p = p * z - 1.0 / 15.0;
    p = p * z + 1.0 / 13.0;
    p = p * z - 1.0 / 11.0;
    p = p * z + 1.0 / 9.0;
    p = p * z - 1.0 / 7.0;
    p = p * z + 1.0 / 5.0;
    p = p * z - 1.0 / 3.0;
    return u + u * z * p;
}

// atan of t in [0, 1]: around pi/6 above 2 - sqrt(3), so the series argument stays small.
[[nodiscard]] double atan_unit(double t) noexcept {
    if (t <= kTwoMinusSqrt3) {
        return atan_poly(t);
    }
    return kPiOver6 + atan_poly((t * kSqrt3 - 1.0) / (t + kSqrt3));
}

struct SinCosD {
    double sin = 0.0;
    double cos = 1.0;
};

[[nodiscard]] SinCosD sin_cos_d(double x) noexcept {
    if (std::fabs(x) > kReduceLimit) {
        x = std::fmod(x, kTwoPi);
    }
    const double k = std::floor(x * kTwoOverPi + 0.5);
    const double r = (((x - k * kPio2Part1) - k * kPio2Part2) - k * kPio2Part3) - k * kPio2Part3Tail;
    const double s = sin_poly(r);
    const double c = cos_poly(r);
    const auto quadrant = static_cast<std::int64_t>(k) & 3;
    switch (quadrant) {
    case 0:
        return {s, c};
    case 1:
        return {c, -s};
    case 2:
        return {-s, -c};
    default:
        return {-c, s};
    }
}

}

SinCos det_sin_cos(float radians) noexcept {
    if (!std::isfinite(radians)) {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        return {nan, nan};
    }
    if (radians == 0.0f) {
        return {radians, 1.0f};  // keeps the sign of zero, as sin does
    }
    const SinCosD result = sin_cos_d(static_cast<double>(radians));
    return {static_cast<float>(result.sin), static_cast<float>(result.cos)};
}

float det_sin(float radians) noexcept {
    return det_sin_cos(radians).sin;
}

float det_cos(float radians) noexcept {
    return det_sin_cos(radians).cos;
}

float det_atan2(float y, float x) noexcept {
    if (std::isnan(x) || std::isnan(y)) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const bool negative_y = std::signbit(y);
    const bool negative_x = std::signbit(x);
    double angle = 0.0;
    if (y == 0.0f) {
        // atan2(+-0, x): +-0 for x > 0 or +0, +-pi for x < 0 or -0.
        if (!negative_x) {
            return y;
        }
        angle = kPi;
    } else if (x == 0.0f) {
        angle = kHalfPi;
    } else if (std::isinf(x) || std::isinf(y)) {
        if (std::isinf(x) && std::isinf(y)) {
            angle = negative_x ? kPi - kPiOver4 : kPiOver4;
        } else if (std::isinf(y)) {
            angle = kHalfPi;
        } else {
            angle = negative_x ? kPi : 0.0;
        }
    } else {
        const double ax = std::fabs(static_cast<double>(x));
        const double ay = std::fabs(static_cast<double>(y));
        // Float magnitudes in double: the ratio neither overflows nor underflows to a wrong quadrant.
        if (ay <= ax) {
            angle = atan_unit(ay / ax);
        } else {
            angle = kHalfPi - atan_unit(ax / ay);
        }
        if (negative_x) {
            angle = kPi - angle;
        }
    }
    const float result = static_cast<float>(angle);
    return negative_y ? -result : result;
}

}
