#pragma once

// docs/tech/modules/Net.md

#include <cstdint>

namespace engine::net {

// What the server did with one peer's inputs since it connected. Counted per input, not per message.
struct NetInputStats {
    // Inputs applied at their own tick.
    std::uint64_t applied{0};
    // Ticks with no input from the peer, after its first one: the last input ran again.
    std::uint64_t repeated{0};
    // Inputs that arrived after their tick ran (with a repeated input).
    std::uint64_t late{0};
    // Inputs already held or already applied (the redundant copies of each message).
    std::uint64_t duplicate{0};
    // Inputs for an entity the peer does not own, or that does not exist.
    std::uint64_t rejected{0};
    // Inputs more than `kMaxInputLeadTicks` ahead of the server.
    std::uint64_t too_far_ahead{0};
    // Input messages dropped whole: truncated, over a cap, or an input `read_input` refused.
    std::uint64_t malformed{0};
};

} // namespace engine::net
