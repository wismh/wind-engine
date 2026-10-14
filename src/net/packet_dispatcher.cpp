#include <engine/net/packet_dispatcher.h>

#include <engine/ecs/hierarchy.h>
#include <engine/ecs/transform.h>
#include <engine/net/net_entity_map.h>

namespace engine::net {

void PacketDispatcher::register_handler(PacketType type, PacketHandler handler) {
    handlers_[type] = std::move(handler);
}

void PacketDispatcher::unregister_handler(PacketType type) {
    handlers_.erase(type);
}

bool PacketDispatcher::has_handler(PacketType type) const {
    return handlers_.contains(type);
}

bool PacketDispatcher::dispatch(PeerId from_peer, std::span<const std::uint8_t> payload) {
    ByteReader reader(payload);
    const auto header = read_packet_header(reader);
    if (!header.has_value() || !reader.is_ok()) {
        return false;
    }

    const auto it = handlers_.find(header->type);
    if (it == handlers_.end()) {
        return false;
    }

    it->second(from_peer, reader);
    return reader.is_ok();
}

void PacketDispatcher::clear_handlers() {
    handlers_.clear();
}

std::size_t PacketDispatcher::handler_count() const noexcept {
    return handlers_.size();
}

void register_replication_handlers(ecs::World& world, PacketDispatcher& dispatcher) {
    dispatcher.register_packet_handler<SpawnEntityPacket>(
        PacketType::SpawnEntity, [&world](PeerId /*from_peer*/, const SpawnEntityPacket& pkt) {
            if (pkt.net_id == kInvalidNetId) {
                return;
            }
            if (find_entity(world, pkt.net_id).has_value()) {
                return;
            }

            const ecs::Entity e = world.create();
            bind_net_entity(world, e, pkt.net_id);

            Transform transform;
            transform.position = pkt.position;
            transform.rotation = pkt.rotation;
            transform.scale = pkt.scale;
            world.emplace<Transform>(e, transform);

            if (pkt.parent_net_id != kInvalidNetId) {
                set_parent_by_net_id(world, pkt.net_id, pkt.parent_net_id);
            }
        });

    dispatcher.register_packet_handler<DespawnEntityPacket>(
        PacketType::DespawnEntity, [&world](PeerId /*from_peer*/, const DespawnEntityPacket& pkt) {
            const auto entity = find_entity(world, pkt.net_id);
            if (!entity.has_value()) {
                return;
            }
            unbind_net_entity(world, *entity);
            world.destroy(*entity);
        });

    dispatcher.register_packet_handler<SyncTransformPacket>(
        PacketType::SyncTransform, [&world](PeerId /*from_peer*/, const SyncTransformPacket& pkt) {
            const auto entity = find_entity(world, pkt.net_id);
            if (!entity.has_value()) {
                return;
            }
            if (auto* t = world.try_get<Transform>(*entity)) {
                t->position = pkt.position;
                t->rotation = pkt.rotation;
                t->scale = pkt.scale;
            } else {
                Transform new_t;
                new_t.position = pkt.position;
                new_t.rotation = pkt.rotation;
                new_t.scale = pkt.scale;
                world.emplace<Transform>(*entity, new_t);
            }
        });

    dispatcher.register_packet_handler<SyncParentPacket>(
        PacketType::SyncParent, [&world](PeerId /*from_peer*/, const SyncParentPacket& pkt) {
            const auto child = find_entity(world, pkt.child_net_id);
            if (!child.has_value()) {
                return;
            }

            if (pkt.parent_net_id != kInvalidNetId) {
                set_parent_by_net_id(world, pkt.child_net_id, pkt.parent_net_id);
            } else {
                ecs::clear_parent(world, *child);
            }
        });
}

} // namespace engine::net
