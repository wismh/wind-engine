#pragma once

// docs/tech/modules/Net.md

#include <cstdint>

namespace engine::net {

// What a client's reconciliation did since it connected.
struct PredictionStats {
    // Server states that matched the prediction: nothing replayed.
    std::uint64_t matched{0};
    // Server states that differed: the entity was rewound and its inputs replayed.
    std::uint64_t replays{0};
    // Steps run by those replays.
    std::uint64_t replayed_ticks{0};
    // The server state was taken as is: older than the ring, or the client tick jumped forward.
    std::uint64_t snaps{0};
};

} // namespace engine::net
