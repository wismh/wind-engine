#pragma once

// docs/tech/modules/Net.md#reliable-payloads

#include <cstdint>

namespace engine::net {

struct NetPayloadOptions {
    // Higher goes first. A waiting payload gains `NetServerConfig::payload_aging_per_second` a second.
    std::int32_t priority{0};
    // The bytes are the output of `engine::compress` (<engine/core/compression.h>). The client decompresses them
    // before its handler runs. The engine never compresses on its own: do it on a job, once for all peers.
    bool compressed{false};
};

} // namespace engine::net
