#pragma once

// docs/tech/modules/Net.md#reliable-payloads

#include <engine/net/net_payload_id.h>
#include <engine/net/network_transport.h>

namespace engine::net {

// Sent on the server world when a client has handled a payload.
struct NetPayloadDeliveredEvent {
    PeerId peer{kInvalidPeerId};
    PayloadId id{kNoPayload};
};

} // namespace engine::net
