#pragma once

// docs/tech/modules/Net.md

namespace engine::net {

struct NetClientConfig {
    // Seconds the client renders behind the newest snapshot. Covers a lost snapshot or two and arrival jitter:
    // at 30 snapshots a second, 0.1 s is three snapshot gaps.
    float interpolation_delay{0.1f};
};

} // namespace engine::net
