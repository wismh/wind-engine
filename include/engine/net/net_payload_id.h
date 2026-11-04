#pragma once

// docs/tech/modules/Net.md#reliable-payloads

#include <cstdint>

namespace engine::net {

// One queued payload on the server. Never reused while the server lives.
using PayloadId = std::uint32_t;
inline constexpr PayloadId kNoPayload = 0;

} // namespace engine::net
