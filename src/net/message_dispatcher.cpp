#include <engine/net/message_dispatcher.h>

namespace engine::net {

void MessageDispatcher::register_handler(MessageId id, Handler handler) {
    if (id == kInvalidMessageId || !handler) {
        return;
    }
    handlers_[id] = std::move(handler);
}

void MessageDispatcher::unregister_handler(MessageId id) {
    handlers_.erase(id);
}

bool MessageDispatcher::dispatch(PeerId from, std::span<const std::uint8_t> payload) {
    ByteReader reader(payload);
    const auto id = reader.read_u16();
    if (!id.has_value() || *id == kInvalidMessageId) {
        return false;
    }

    auto it = handlers_.find(*id);
    if (it == handlers_.end()) {
        return false;
    }

    it->second(from, reader);
    return true;
}

bool MessageDispatcher::send(INetworkTransport& transport, PeerId target, MessageId id,
    std::span<const std::uint8_t> payload, DeliveryMode mode, std::uint8_t channel) {
    if (id == kInvalidMessageId) {
        return false;
    }

    ByteWriter writer(sizeof(MessageId) + payload.size());
    writer.write_u16(id);
    writer.append(payload);

    return transport.send(target, writer.bytes(), mode, channel);
}

} // namespace engine::net
