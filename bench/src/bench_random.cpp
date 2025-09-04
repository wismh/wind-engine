#include "bench_random.h"

namespace bench {

std::uint64_t BenchRandom::next() {
    state_ += 0x9e3779b97f4a7c15ull;
    std::uint64_t z = state_;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

int BenchRandom::between(int low, int high) {
    if (high <= low) {
        return low;
    }
    const std::uint64_t span = static_cast<std::uint64_t>(static_cast<std::int64_t>(high) - low) + 1;
    return static_cast<int>(static_cast<std::int64_t>(low) + static_cast<std::int64_t>(next() % span));
}

double BenchRandom::unit() {
    return static_cast<double>(next() >> 11) * 0x1.0p-53;
}

}
