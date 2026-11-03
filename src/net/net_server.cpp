#include <engine/net/net_server.h>

#include "net/snapshot_codec.h"
#include "net/snapshot_state.h"

#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/schedule.h>
#include <engine/log.h>
#include <engine/net/byte_stream.h>
#include <engine/net/net_entity_map.h>
#include <engine/net/net_id.h>
#include <engine/net/net_messages.h>
#include <engine/net/net_peer_connected_event.h>
#include <engine/net/net_peer_disconnected_event.h>
#include <engine/net/replication_registry.h>

#include <cmath>
#include <deque>
#include <map>
#include <string>
#include <utility>

namespace engine::net {

struct NetServer::Impl {
    struct SentSnapshot {
        Tick tick{kNoTick};
        std::shared_ptr<const SnapshotState> state{};
    };

    struct Peer {
        Tick acked{kNoTick};
        // Snapshots sent to this peer that are still inside the window, oldest first.
        std::deque<SentSnapshot> sent{};
        bool warned_size{false};

        [[nodiscard]] const SentSnapshot* find(Tick tick) const {
            for (const SentSnapshot& snapshot : sent) {
                if (snapshot.tick == tick) {
                    return &snapshot;
                }
            }
            return nullptr;
        }
    };

    INetworkTransport* transport{nullptr};
    NetServerConfig config{};
    bool systems_installed{false};
    Tick tick{kNoTick};
    MessageDispatcher dispatcher{};
    NetIdGenerator ids{};
    std::map<PeerId, Peer> peers{};
};

NetServer::NetServer()
    : impl_(std::make_unique<Impl>()) {}

NetServer::~NetServer() = default;

Tick NetServer::tick() const noexcept {
    return impl_->tick;
}

const NetServerConfig& NetServer::config() const noexcept {
    return impl_->config;
}

MessageDispatcher& NetServer::dispatcher() noexcept {
    return impl_->dispatcher;
}

std::vector<PeerId> NetServer::peers() const {
    std::vector<PeerId> out;
    out.reserve(impl_->peers.size());
    for (const auto& [peer, state] : impl_->peers) {
        out.push_back(peer);
    }
    return out;
}

std::optional<Tick> NetServer::acked_tick(PeerId peer) const {
    const auto it = impl_->peers.find(peer);
    if (it == impl_->peers.end() || it->second.acked == kNoTick) {
        return std::nullopt;
    }
    return it->second.acked;
}

void install_net_server(ecs::World& world, INetworkTransport& transport, NetServerConfig config) {
    NetServer::Impl& server = *world.ctx<NetServer>().impl_;
    server.transport = &transport;
    server.config = config;
    if (server.config.snapshot_interval == 0) {
        server.config.snapshot_interval = 1;
    }
    (void)world.ctx<ReplicationRegistry>();
    (void)world.ctx<NetEntityMap>();
    if (server.systems_installed) {
        return;
    }
    server.systems_installed = true;
    world.add_system(ecs::Schedule::Fixed, ecs::Phase::NetReceive, [](ecs::World& w) { run_net_server_receive(w); });
    world.add_system(ecs::Schedule::Fixed, ecs::Phase::NetSend, [](ecs::World& w) { run_net_server_send(w); });
}

namespace {

void send_welcome(ecs::World& world, NetServer::Impl& server, PeerId peer) {
    ByteWriter payload;
    payload.write_u16(kNetProtocolVersion);
    payload.write_u32(world.ctx<ReplicationRegistry>().schema_hash());
    payload.write_u16(static_cast<std::uint16_t>(std::lround(1.0 / static_cast<double>(kFixed))));
    payload.write_u16(server.config.snapshot_interval);
    payload.write_u64(peer);
    payload.write_u32(server.tick);
    MessageDispatcher::send(*server.transport, peer, kWelcomeMessageId, payload.bytes(), DeliveryMode::ReliableOrdered);
}

void receive_ack(NetServer::Impl& server, PeerId peer, ByteReader& reader) {
    const auto tick = reader.read_u32();
    const auto it = server.peers.find(peer);
    if (!tick.has_value() || it == server.peers.end()) {
        return;
    }
    // Only a snapshot this peer was sent and that is still held can become its baseline.
    if (*tick > it->second.acked && it->second.find(*tick) != nullptr) {
        it->second.acked = *tick;
    }
}

} // namespace

void run_net_server_receive(ecs::World& world) {
    NetServer::Impl& server = *world.ctx<NetServer>().impl_;
    if (server.transport == nullptr) {
        return;
    }
    server.transport->poll_events([&](const TransportEvent& event) {
        switch (event.type) {
        case TransportEventType::Connected:
            server.peers.insert_or_assign(event.peer_id, NetServer::Impl::Peer{});
            send_welcome(world, server, event.peer_id);
            ecs::EventWriter<NetPeerConnectedEvent>(world).send(NetPeerConnectedEvent{.peer_id = event.peer_id});
            break;
        case TransportEventType::Disconnected:
            server.peers.erase(event.peer_id);
            ecs::EventWriter<NetPeerDisconnectedEvent>(world).send(
                    NetPeerDisconnectedEvent{.peer_id = event.peer_id, .reason = event.reason});
            break;
        case TransportEventType::DataReceived: {
            ByteReader reader(event.payload);
            const auto id = reader.read_u16();
            if (!id.has_value()) {
                break;
            }
            if (*id == kSnapshotAckMessageId) {
                receive_ack(server, event.peer_id, reader);
            } else if (*id < kEngineMessageFirst || *id > kEngineMessageLast) {
                server.dispatcher.dispatch(event.peer_id, event.payload);
            }
            break;
        }
        }
    });
    ++server.tick;
}

void run_net_server_send(ecs::World& world) {
    NetServer::Impl& server = *world.ctx<NetServer>().impl_;
    if (server.transport == nullptr || server.tick == kNoTick) {
        return;
    }
    (void)world.ctx<NetEntityMap>().unbind_dead(world);
    if (server.tick % server.config.snapshot_interval != 0 || server.peers.empty()) {
        return;
    }

    const auto state = std::make_shared<const SnapshotState>(
            capture_snapshot(world, world.ctx<ReplicationRegistry>(), server.tick));
    for (auto& [peer, client] : server.peers) {
        std::erase_if(client.sent, [&server](const NetServer::Impl::SentSnapshot& sent) {
            return sent.tick + kSnapshotWindowTicks <= server.tick;
        });
        const NetServer::Impl::SentSnapshot* baseline = client.find(client.acked);
        const auto bytes = encode_snapshot(*state, baseline != nullptr ? baseline->state.get() : nullptr);
        if (bytes.size() > kSnapshotMtuBudget && !client.warned_size) {
            client.warned_size = true;
            log::warn("net: snapshot " + std::to_string(bytes.size()) + " bytes for peer " + std::to_string(peer) +
                      " exceeds one packet (" + std::to_string(kSnapshotMtuBudget) +
                      "); it is split and lost whole if any piece is lost");
        }
        server.transport->send(peer, bytes, DeliveryMode::UnreliableImmediate);
        client.sent.push_back(NetServer::Impl::SentSnapshot{.tick = server.tick, .state = state});
    }
}

NetId replicate(ecs::World& world, ecs::Entity entity, PeerId owner) {
    NetServer::Impl& server = *world.ctx<NetServer>().impl_;
    auto& map = world.ctx<NetEntityMap>();
    NetId id = server.ids.next();
    while (map.contains(id)) {
        id = server.ids.next();
    }
    if (!bind_net_entity(world, entity, id, owner)) {
        return kInvalidNetId;
    }
    return id;
}

} // namespace engine::net
