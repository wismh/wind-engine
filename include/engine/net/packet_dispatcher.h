#pragma once

#include <engine/ecs/world.h>
#include <engine/net/byte_stream.h>
#include <engine/net/network_transport.h>
#include <engine/net/packet_types.h>

#include <functional>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>

namespace engine::net {

class PacketDispatcher {
public:
    using PacketHandler = std::function<void(PeerId from_peer, ByteReader& reader)>;

    PacketDispatcher() = default;

    void register_handler(PacketType type, PacketHandler handler);
    void unregister_handler(PacketType type);
    [[nodiscard]] bool has_handler(PacketType type) const;

    template <typename PacketT>
    void register_packet_handler(
        PacketType type, std::function<void(PeerId from_peer, const PacketT& packet)> handler) {
        register_handler(type, [h = std::move(handler)](PeerId from_peer, ByteReader& reader) {
            auto pkt = PacketT::deserialize(reader);
            if (pkt.has_value()) {
                h(from_peer, *pkt);
            }
        });
    }

    bool dispatch(PeerId from_peer, std::span<const std::uint8_t> payload);

    void clear_handlers();
    [[nodiscard]] std::size_t handler_count() const noexcept;

private:
    std::unordered_map<PacketType, PacketHandler> handlers_{};
};

void register_replication_handlers(ecs::World& world, PacketDispatcher& dispatcher);

} // namespace engine::net
