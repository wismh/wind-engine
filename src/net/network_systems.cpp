#include <engine/net/network_systems.h>

#include <engine/ecs/events.h>
#include <engine/ecs/transform.h>
#include <engine/net/byte_stream.h>
#include <engine/net/network_events.h>
#include <engine/net/network_identity.h>
#include <engine/net/replicated_transform.h>

#include <algorithm>

namespace engine::net {

void run_network_receive(ecs::World& world, INetworkTransport& transport, MessageDispatcher& dispatcher) {
    transport.poll_events([&](const TransportEvent& event) {
        switch (event.type) {
        case TransportEventType::Connected:
            ecs::EventWriter<NetPeerConnectedEvent>(world).send(NetPeerConnectedEvent{.peer_id = event.peer_id});
            break;
        case TransportEventType::Disconnected:
            ecs::EventWriter<NetPeerDisconnectedEvent>(world).send(
                NetPeerDisconnectedEvent{.peer_id = event.peer_id, .reason = event.reason});
            break;
        case TransportEventType::DataReceived:
            dispatcher.dispatch(event.peer_id, event.payload);
            break;
        }
    });
}

void run_network_server_replication(ecs::World& world, INetworkTransport& transport,
    const std::vector<PeerId>& clients) {
    if (clients.empty()) {
        return;
    }

    std::vector<std::pair<NetIdentity, Transform>> entities_to_replicate;
    for (const auto entity : world.view<NetIdentity, Transform>()) {
        const auto& id = world.get<NetIdentity>(entity);
        const auto& tr = world.get<Transform>(entity);
        entities_to_replicate.emplace_back(id, tr);
    }

    if (entities_to_replicate.empty()) {
        return;
    }

    ByteWriter writer;
    writer.write_u32(static_cast<std::uint32_t>(entities_to_replicate.size()));
    for (const auto& [id, tr] : entities_to_replicate) {
        writer.write_u32(id.id);
        writer.write_vec3(tr.position);
        writer.write_float(tr.rotation.w);
        writer.write_float(tr.rotation.x);
        writer.write_float(tr.rotation.y);
        writer.write_float(tr.rotation.z);
    }

    for (const auto client_id : clients) {
        MessageDispatcher::send(transport, client_id, kSnapshotMessageId, writer.bytes(),
            DeliveryMode::UnreliableSequenced);
    }
}

void run_network_interpolation(ecs::World& world, float dt) {
    if (dt <= 0.0f) {
        return;
    }

    for (const auto entity : world.view<ReplicatedTransform, Transform>()) {
        auto& replicated = world.get<ReplicatedTransform>(entity);
        auto& tr = world.get<Transform>(entity);

        const float factor = std::clamp(dt * replicated.lerp_speed, 0.0f, 1.0f);
        tr.position = glm::mix(tr.position, replicated.target_position, factor);
        tr.rotation = glm::slerp(tr.rotation, replicated.target_rotation, factor);
    }
}

void setup_client_snapshot_receiver(ecs::World& world, MessageDispatcher& dispatcher) {
    dispatcher.register_handler(kSnapshotMessageId, [&world](PeerId, ByteReader& reader) {
        const auto count = reader.read_u32();
        if (!count.has_value()) {
            return;
        }

        for (std::uint32_t i = 0; i < *count; ++i) {
            const auto net_id = reader.read_u32();
            const auto pos = reader.read_vec3();
            const auto rot_w = reader.read_float();
            const auto rot_x = reader.read_float();
            const auto rot_y = reader.read_float();
            const auto rot_z = reader.read_float();

            if (!net_id || !pos || !rot_w || !rot_x || !rot_y || !rot_z) {
                break;
            }

            const glm::quat rot(*rot_w, *rot_x, *rot_y, *rot_z);

            for (const auto entity : world.view<NetIdentity>()) {
                const auto& id = world.get<NetIdentity>(entity);
                if (id.id == *net_id) {
                    if (auto* repl = world.try_get<ReplicatedTransform>(entity)) {
                        repl->target_position = *pos;
                        repl->target_rotation = rot;
                    } else {
                        world.emplace<ReplicatedTransform>(entity, ReplicatedTransform{
                            .target_position = *pos,
                            .target_rotation = rot,
                            .lerp_speed = 15.0f,
                        });
                    }
                    break;
                }
            }
        }
    });
}

} // namespace engine::net
