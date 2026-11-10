#pragma once

// docs/tech/architecture/Principles.md (Deterministic simulation)

namespace engine {

struct SinCos {
    float sin = 0.0f;
    float cos = 1.0f;
};

// Trigonometry that gives the same bits on every supported platform and compiler. The C library's `sin`, `cos` and
// `atan2` differ between MSVC, glibc, libc++ and Emscripten in the last bits, which is enough to send two peers of a
// simulation apart. These are built from `+ - * /` only, in double, and are correctly rounded to float for almost
// every input (within one float ulp always). Engine simulation (everything on Schedule::Fixed) uses them; game
// simulation that must stay in lockstep should too. A non-finite input gives NaN.
[[nodiscard]] SinCos det_sin_cos(float radians) noexcept;
[[nodiscard]] float det_sin(float radians) noexcept;
[[nodiscard]] float det_cos(float radians) noexcept;
// The angle of (x, y) in [-pi, pi], as `std::atan2`, signed zeros and infinities included. NaN in, NaN out.
[[nodiscard]] float det_atan2(float y, float x) noexcept;

}
