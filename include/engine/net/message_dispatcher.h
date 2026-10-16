#pragma once

#include <engine/net/byte_stream.h>
#include <engine/net/network_transport.h>

#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>

namespace engine::net {

using MessageId = std::uint16_t;
inline constexpr MessageId kInvalidMessageId = 0;

class MessageDispatcher {
public:
    using Handler = std::function<void(PeerId from, ByteReader& reader)>;

    MessageDispatcher() = default;

    void register_handler(MessageId id, Handler handler);
    void unregister_handler(MessageId id);

    bool dispatch(PeerId from, std::span<const std::uint8_t> payload);

    static bool send(INetworkTransport& transport, PeerId target, MessageId id,
        std::span<const std::uint8_t> payload, DeliveryMode mode = DeliveryMode::ReliableOrdered,
        std::uint8_t channel = 0);

private:
    std::unordered_map<MessageId, Handler> handlers_{};
};

} // namespace engine::net
