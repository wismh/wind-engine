#pragma once

// docs/tech/modules/Net.md#reliable-payloads

#include <cstddef>

namespace engine::net {

struct NetPayloadProgress {
    // Handed to the transport so far.
    std::size_t sent_bytes{0};
    std::size_t total_bytes{0};
    // All of it was sent; the client's ack has not arrived.
    bool awaiting_ack{false};
};

} // namespace engine::net
