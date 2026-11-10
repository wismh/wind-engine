#pragma once

// docs/tech/architecture/Principles.md (Deterministic simulation): hashes of simulation state.

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <bit>
#include <cstdint>

namespace engine {

// FNV-1a 64 over little-endian bytes. Floats go in by their bits, so -0 and +0 differ and NaN payloads count: the
// hash says whether two states are the same bits, not the same values.
class StateHasher {
public:
    void u8(std::uint8_t value) noexcept {
        hash_ ^= value;
        hash_ *= kPrime;
    }
    void u32(std::uint32_t value) noexcept {
        for (int shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }
    void u64(std::uint64_t value) noexcept {
        u32(static_cast<std::uint32_t>(value));
        u32(static_cast<std::uint32_t>(value >> 32));
    }
    void i32(std::int32_t value) noexcept {
        u32(static_cast<std::uint32_t>(value));
    }
    void f32(float value) noexcept {
        u32(std::bit_cast<std::uint32_t>(value));
    }
    void vec2(glm::vec2 value) noexcept {
        f32(value.x);
        f32(value.y);
    }
    void vec3(glm::vec3 value) noexcept {
        f32(value.x);
        f32(value.y);
        f32(value.z);
    }
    void quat(const glm::quat& value) noexcept {
        f32(value.x);
        f32(value.y);
        f32(value.z);
        f32(value.w);
    }

    [[nodiscard]] std::uint64_t value() const noexcept {
        return hash_;
    }

private:
    static constexpr std::uint64_t kOffsetBasis = 14695981039346656037ull;
    static constexpr std::uint64_t kPrime = 1099511628211ull;
    std::uint64_t hash_ = kOffsetBasis;
};

}
