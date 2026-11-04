#pragma once

// docs/tech/modules/Net.md#interest

namespace engine::net {

// On a server entity: every peer is told about it, whatever `NetInterest` says.
struct NetAlwaysRelevant {};

} // namespace engine::net
