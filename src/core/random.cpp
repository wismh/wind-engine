#include <engine/core/random.h>

#include <algorithm>
#include <chrono>

namespace engine {

namespace {

constexpr std::uint64_t kMultiplier = 6364136223846793005ULL;

std::uint64_t clock_seed() {
    return static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()) ^
           static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

}

Random::Random() {
    seed(clock_seed());
}

Random::Random(std::uint64_t seed_value, std::uint64_t stream) {
    seed(seed_value, stream);
}

void Random::seed(std::uint64_t seed_value, std::uint64_t stream) {
    state_ = 0;
    inc_ = (stream << 1u) | 1u;
    (void)next_u32();
    state_ += seed_value;
    (void)next_u32();
}

std::uint32_t Random::next_u32() {
    const std::uint64_t old = state_;
    state_ = old * kMultiplier + inc_;
    const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
    const auto rot = static_cast<std::uint32_t>(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
}

std::uint64_t Random::next_u64() {
    const std::uint64_t high = next_u32();
    return (high << 32u) | next_u32();
}

float Random::next_float() {
    return static_cast<float>(next_u32() >> 8u) * (1.0f / 16777216.0f);
}

int Random::uniform(int lo, int hi) {
    assert(lo <= hi);
    if (lo >= hi) {
        return lo;
    }
    const std::uint64_t span = static_cast<std::uint64_t>(static_cast<std::int64_t>(hi) - lo) + 1u;
    if (span > 0xFFFFFFFFULL) {
        // Only the full int range: every 32-bit pattern is a valid answer.
        return static_cast<int>(static_cast<std::int64_t>(lo) + static_cast<std::int64_t>(next_u32()));
    }
    // Rejection sampling: drop the draws from the incomplete last bucket so every value is equally likely.
    const auto bound = static_cast<std::uint32_t>(span);
    const std::uint32_t threshold = (0u - bound) % bound;
    std::uint32_t draw = next_u32();
    while (draw < threshold) {
        draw = next_u32();
    }
    return static_cast<int>(static_cast<std::int64_t>(lo) + static_cast<std::int64_t>(draw % bound));
}

float Random::uniform(float lo, float hi) {
    assert(lo <= hi);
    return lo + (hi - lo) * next_float();
}

bool Random::chance(float p) {
    return next_float() < std::clamp(p, 0.0f, 1.0f);
}

}
