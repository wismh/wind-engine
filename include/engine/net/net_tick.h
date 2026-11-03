#pragma once

// docs/tech/modules/Net.md

#include <cstdint>

namespace engine::net {

// One server fixed step. The first tick is 1; `kNoTick` means "none yet" and, as a snapshot baseline, "full".
using Tick = std::uint32_t;
inline constexpr Tick kNoTick = 0;

} // namespace engine::net
