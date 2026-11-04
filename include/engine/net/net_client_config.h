#pragma once

// docs/tech/modules/Net.md

#include <cstdint>

namespace engine::net {

struct NetClientConfig {
    // Seconds the client renders behind the newest snapshot. Covers a lost snapshot or two and arrival jitter:
    // at 30 snapshots a second, 0.1 s is three snapshot gaps.
    float interpolation_delay{0.1f};
    // Inputs in each Input message, newest first: up to this many minus one consecutive lost messages lose no
    // input. Clamped to 1..32.
    std::uint8_t input_redundancy{8};
    // Ticks a predicting client's inputs should arrive before the server runs them: the server's input jitter
    // buffer. More absorbs more jitter and puts the client's prediction that many ticks further ahead.
    std::uint8_t input_buffer_ticks{2};
};

} // namespace engine::net
