#pragma once

// docs/tech/modules/Core.md

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ranges>

namespace engine {

// A small seedable PRNG (PCG32, XSH-RR) for gameplay and VFX. The same seed (and stream) gives the same sequence on
// every platform and compiler: the integer and float mappings are written here, not taken from `<random>`, whose
// distributions differ between standard libraries. Not for cryptography. Main thread only, like the world it lives in.
//
// A world holds one as `world.ctx<Random>()`. A default-constructed `Random` is seeded from the clock; call `seed` (or
// return a number from `IGame::random_seed`) for a fixed sequence in tests and replays.
class Random {
public:
    // Seeded from the clock: different on every run.
    Random();
    explicit Random(std::uint64_t seed, std::uint64_t stream = 0);

    // Restarts the sequence. Two streams of one seed are independent sequences.
    void seed(std::uint64_t seed, std::uint64_t stream = 0);

    [[nodiscard]] std::uint32_t next_u32();
    [[nodiscard]] std::uint64_t next_u64();

    // [0, 1) with 24 random bits.
    [[nodiscard]] float next_float();

    // lo..hi inclusive, without modulo bias. `lo > hi` is a programmer error.
    [[nodiscard]] int uniform(int lo, int hi);
    // [lo, hi). `lo > hi` is a programmer error.
    [[nodiscard]] float uniform(float lo, float hi);

    // true with probability p (clamped to [0, 1]).
    [[nodiscard]] bool chance(float p);

    // One element of a non-empty array, vector, or span.
    template<std::ranges::random_access_range Range>
    [[nodiscard]] decltype(auto) pick(Range&& values) {
        assert(std::ranges::size(values) > 0);
        return values[static_cast<std::ptrdiff_t>(uniform(0, static_cast<int>(std::ranges::size(values)) - 1))];
    }

private:
    std::uint64_t state_ = 0;
    std::uint64_t inc_ = 1;
};

}
