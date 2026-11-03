#pragma once

// docs/tech/modules/Net.md

#include <cstdint>

namespace engine::net {

struct NetServerConfig {
    // Ticks between snapshots. 2 at the 60 Hz fixed step is 30 snapshots a second. 0 counts as 1.
    std::uint16_t snapshot_interval{2};
};

} // namespace engine::net
