#pragma once

// docs/tech/modules/Net.md#budget

#include <cstddef>

namespace engine::net {

// What the server did for one peer.
struct NetPeerStats {
    // Entities relevant to the peer at its last snapshot.
    std::size_t relevant{0};
    // Entities with something to send that its last snapshot left out for lack of room.
    std::size_t deferred{0};
    std::size_t last_snapshot_bytes{0};
    // Payload bytes handed to the transport since the peer connected, headers included.
    std::size_t payload_bytes_sent{0};
};

} // namespace engine::net
