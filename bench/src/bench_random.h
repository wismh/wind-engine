#pragma once

#include <cstdint>

namespace bench {

// The seed every scene's data starts from, so two runs of a case draw the same rows, text, and units.
inline constexpr std::uint64_t kBenchSeed = 0x57494e4442454e43ull;

// SplitMix64. The standard library's distributions differ between implementations; this sequence does not.
class BenchRandom {
public:
    explicit BenchRandom(std::uint64_t seed) : state_(seed) {}

    [[nodiscard]] std::uint64_t next();
    // A whole number in [low, high]. `high` below `low` gives `low`.
    [[nodiscard]] int between(int low, int high);
    // A number in [0, 1).
    [[nodiscard]] double unit();

private:
    std::uint64_t state_;
};

}
