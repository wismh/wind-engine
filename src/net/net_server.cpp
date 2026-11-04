#include <engine/net/net_server.h>

#include "net/input_buffer.h"
#include "net/input_message.h"
#include "net/net_schema.h"
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
#include <engine/net/net_prediction.h>
#include <engine/net/net_peer_connected_event.h>
#include <engine/net/net_peer_disconnected_event.h>
#include <engine/net/replication_registry.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

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
        // One input buffer per controlled entity of this peer.
        std::map<NetId, InputBuffer> inputs{};
        NetInputStats stats{};
        // The newest tick that ran with this peer's own input.
        Tick input_tick{kNoTick};
        // The smallest (newest input tick of a message - the tick that runs next) over the input messages
        // received since this peer's last snapshot: how early its inputs arrive.
        std::optional<std::int64_t> slack{};
        bool warned_rejected{false};

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

std::optional<NetInputStats> NetServer::input_stats(PeerId peer) const {
    const auto it = impl_->peers.find(peer);
    if (it == impl_->peers.end()) {
        return std::nullopt;
    }
    return it->second.stats;
}

std::optional<Tick> NetServer::input_tick(PeerId peer) const {
    const auto it = impl_->peers.find(peer);
    if (it == impl_->peers.end() || it->second.input_tick == kNoTick) {
        return std::nullopt;
    }
    return it->second.input_tick;
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
    payload.write_u32(welcome_schema_hash(world));
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

PredictionChannel* prediction_channel(ecs::World& world) {
    NetPrediction* prediction = world.try_ctx<NetPrediction>();
    return prediction != nullptr ? prediction->channel.get() : nullptr;
}

void receive_input(ecs::World& world, NetServer::Impl& server, PeerId peer, ByteReader& reader) {
    PredictionChannel* channel = prediction_channel(world);
    const auto it = server.peers.find(peer);
    if (channel == nullptr || it == server.peers.end()) {
        return;
    }
    NetServer::Impl::Peer& client = it->second;
    const auto message = InputMessage::decode(reader);
    bool valid = message.has_value();
    for (std::size_t e = 0; valid && e < message->entities.size(); ++e) {
        for (const auto& input : message->entities[e].inputs) {
            if (!channel->valid_input(input)) {
                valid = false;
                break;
            }
        }
    }
    if (!valid) {
        ++client.stats.malformed;
        return;
    }
    bool accepted = false;
    for (const InputMessage::Entity& section : message->entities) {
        const auto entity = find_entity(world, section.id);
        const NetIdentity* identity = entity.has_value() ? world.try_get<NetIdentity>(*entity) : nullptr;
        if (identity == nullptr || identity->owner != peer) {
            client.stats.rejected += section.inputs.size();
            if (!client.warned_rejected) {
                client.warned_rejected = true;
                log::warn("net: peer " + std::to_string(peer) + " sent input for entity " +
                          std::to_string(section.id) + ", which it does not own; ignored");
            }
            continue;
        }
        accepted = true;
        InputBuffer& buffer = client.inputs.try_emplace(section.id, server.tick).first->second;
        for (std::size_t i = 0; i < section.inputs.size(); ++i) {
            const Tick tick = message->newest - static_cast<Tick>(i);
            switch (buffer.insert(tick, section.inputs[i], server.tick)) {
            case InputBuffer::Insert::Stored:
                break;
            case InputBuffer::Insert::Duplicate:
                ++client.stats.duplicate;
                break;
            case InputBuffer::Insert::Late:
                ++client.stats.late;
                break;
            case InputBuffer::Insert::TooFarAhead:
                ++client.stats.too_far_ahead;
                break;
            }
        }
    }
    if (accepted) {
        const std::int64_t early =
                static_cast<std::int64_t>(message->newest) - (static_cast<std::int64_t>(server.tick) + 1);
        client.slack = client.slack.has_value() ? std::min(*client.slack, early) : early;
    }
}

// Steps every entity a connected client owns with that client's input for this tick, in `NetId` order.
void step_controlled(ecs::World& world, NetServer::Impl& server) {
    PredictionChannel* channel = prediction_channel(world);
    if (channel == nullptr) {
        return;
    }
    std::vector
<std::pair<NetId, ecs::Entity>> controlled;
    for (const ecs::Entity entity : world.view<NetIdentity>()) {
        const NetIdentity& identity = world.get<NetIdentity>(entity);
        if (identity.id != kInvalidNetId && identity.owner != kServerPeerId && server.peers.contains(identity.owner)) {
            controlled.emplace_back(identity.id, entity);
        }
    }
    std::sort(controlled.begin(), controlled.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    // A stream whose entity is gone, or now owned by someone else, is dropped.
    for (auto& [peer, client] : server.peers) {
        std::erase_if(client.inputs, [&world, &controlled, peer](const auto& stream) {
            const auto it = std::lower_bound(controlled.begin(), controlled.end(), stream.first,
                    [](const auto& item, NetId id) { return item.first < id; });
            return it == controlled.end() || it->first != stream.first ||
                   world.get<NetIdentity>(it->second).owner != peer;
        });
    }

    for (const auto& [id, entity] : controlled) {
        NetServer::Impl::Peer& client = server.peers.at(world.get<NetIdentity>(entity).owner);
        InputBuffer& buffer = client.inputs.try_emplace(id, server.tick - 1).first->second;
        const InputBuffer::Taken taken = buffer.take(server.tick);
        if (taken.real) {
            ++client.stats.applied;
            client.input_tick = server.tick;
        } else if (taken.repeated) {
            ++client.stats.repeated;
        }
        channel->step_encoded(world, entity, taken.bytes);
    }
}

[[nodiscard]] SnapshotPeerHeader take_peer_header(NetServer::Impl::Peer& client) {
    SnapshotPeerHeader header{.input_tick = client.input_tick};
    if (client.slack.has_value()) {
        header.input_slack = static_cast<std::int8_t>(std::clamp<std::int64_t>(*client.slack, -127, 127));
    }
    client.slack.reset();
    return header;
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
            } else if (*id == kInputMessageId) {
                receive_input(world, server, event.peer_id, reader);
            } else if (*id < kEngineMessageFirst || *id > kEngineMessageLast) {
                server.dispatcher.dispatch(event.peer_id, event.payload);
            }
            break;
        }
        }
    });
    ++server.tick;
    step_controlled(world, server);
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
        const auto bytes = encode_snapshot(
                *state, baseline != nullptr ? baseline->state.get() : nullptr, take_peer_header(client));

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
