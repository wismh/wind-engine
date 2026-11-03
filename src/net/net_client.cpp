#include <engine/net/net_client.h>

#include "net/snapshot_buffer.h"
#include "net/snapshot_clock.h"
#include "net/snapshot_codec.h"
#include "net/snapshot_state.h"

#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/schedule.h>
#include <engine/log.h>
#include <engine/net/byte_stream.h>
#include <engine/net/net_entity_despawned_event.h>
#include <engine/net/net_entity_map.h>
#include <engine/net/net_entity_spawned_event.h>
#include <engine/net/net_messages.h>
#include <engine/net/net_peer_connected_event.h>
#include <engine/net/net_peer_disconnected_event.h>
#include <engine/net/network_identity.h>
#include <engine/net/replication_registry.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace engine::net {

struct NetClient::Impl {
    struct Live {
        ecs::Entity entity{};
        // Slots applied last frame, so a slot the server dropped is removed once.
        std::uint32_t applied{0};
    };

    INetworkTransport* transport{nullptr};
    NetClientConfig config{};
    bool systems_installed{false};
    MessageDispatcher dispatcher{};

    bool welcomed{false};
    PeerId local_peer{kInvalidPeerId};
    double tick_seconds{static_cast<double>(kFixed)};
    Tick newest{kNoTick};
    SnapshotBuffer buffer{};
    SnapshotClock clock{};
    std::map<NetId, Live> live{};
};

NetClient::NetClient()
    : impl_(std::make_unique<Impl>()) {}

NetClient::~NetClient() = default;

const NetClientConfig& NetClient::config() const noexcept {
    return impl_->config;
}

bool NetClient::welcomed() const noexcept {
    return impl_->welcomed;
}

PeerId NetClient::local_peer() const noexcept {
    return impl_->local_peer;
}

Tick NetClient::newest_tick() const noexcept {
    return impl_->newest;
}

double NetClient::render_tick() const noexcept {
    if (!impl_->clock.started()) {
        return -1.0;
    }
    return (impl_->clock.now() - static_cast<double>(impl_->config.interpolation_delay)) / impl_->tick_seconds;
}

std::vector<Tick> NetClient::buffered_ticks() const {
    return impl_->buffer.ticks();
}

MessageDispatcher& NetClient::dispatcher() noexcept {
    return impl_->dispatcher;
}

void install_net_client(ecs::World& world, INetworkTransport& transport, NetClientConfig config) {
    NetClient::Impl& client = *world.ctx<NetClient>().impl_;
    client.transport = &transport;
    client.config = config;
    (void)world.ctx<ReplicationRegistry>();
    (void)world.ctx<NetEntityMap>();
    if (client.systems_installed) {
        return;
    }
    client.systems_installed = true;
    world.add_system(ecs::Schedule::Frame, ecs::Phase::NetReceive, [](ecs::World& w) { run_net_client_receive(w); });
    world.add_system(
            ecs::Schedule::Frame, ecs::Phase::NetReceive, [](ecs::World& w) { run_net_client_interpolate(w); });
}

namespace {

void despawn(ecs::World& world, NetId id, ecs::Entity entity) {
    ecs::EventWriter<NetEntityDespawnedEvent>(world).send(NetEntityDespawnedEvent{.entity = entity, .net_id = id});
    world.ctx<NetEntityMap>().unbind(id);
    if (world.valid(entity)) {
        world.destroy(entity);
    }
}

void forget_server(ecs::World& world, NetClient::Impl& client) {
    for (const auto& [id, live] : client.live) {
        despawn(world, id, live.entity);
    }
    client.live.clear();
    client.buffer.clear();
    client.clock.reset();
    client.welcomed = false;
    client.local_peer = kInvalidPeerId;
    client.newest = kNoTick;
}

void receive_welcome(ecs::World& world, NetClient::Impl& client, ByteReader& reader) {
    const auto protocol = reader.read_u16();
    const auto schema = reader.read_u32();
    const auto tick_hz = reader.read_u16();
    const auto interval = reader.read_u16();
    const auto peer = reader.read_u64();
    const auto server_tick = reader.read_u32();
    if (!protocol || !schema || !tick_hz || !interval || !peer || !server_tick || *tick_hz == 0) {
        log::error("net: malformed Welcome; disconnecting");
        client.transport->disconnect(kServerPeerId);
        return;
    }
    if (*protocol != kNetProtocolVersion) {
        log::error("net: server protocol " + std::to_string(*protocol) + ", this client speaks " +
                   std::to_string(kNetProtocolVersion) + "; disconnecting");
        client.transport->disconnect(kServerPeerId);
        return;
    }
    if (*schema != world.ctx<ReplicationRegistry>().schema_hash()) {
        log::error("net: the server replicates a different component list (register_replicated must match on "
                   "both sides); disconnecting");
        client.transport->disconnect(kServerPeerId);
        return;
    }
    client.welcomed = true;
    client.local_peer = *peer;
    client.tick_seconds = 1.0 / static_cast<double>(*tick_hz);
}

void receive_snapshot(ecs::World& world, NetClient::Impl& client, ByteReader& reader) {
    if (!client.welcomed) {
        return;
    }
    Tick tick = kNoTick;
    Tick baseline_tick = kNoTick;
    if (!read_snapshot_header(reader, tick, baseline_tick)) {
        return;
    }
    if (client.buffer.find(tick) == nullptr) {
        const SnapshotState* baseline = nullptr;
        if (baseline_tick != kNoTick) {
            baseline = client.buffer.find(baseline_tick);
            if (baseline == nullptr) {
                return;
            }
        }
        auto state = decode_snapshot(reader, tick, baseline_tick, baseline, world.ctx<ReplicationRegistry>().size());
        if (!state.has_value()) {
            return;
        }
        if (!client.buffer.insert(std::make_shared<const SnapshotState>(std::move(*state)))) {
            return;
        }
        if (tick > client.newest) {
            client.newest = tick;
            client.clock.observe(static_cast<double>(tick) * client.tick_seconds);
        }
    }
    ByteWriter ack;
    ack.write_u32(tick);
    MessageDispatcher::send(
            *client.transport, kServerPeerId, kSnapshotAckMessageId, ack.bytes(), DeliveryMode::Unreliable);
}

} // namespace

void run_net_client_receive(ecs::World& world) {
    NetClient::Impl& client = *world.ctx<NetClient>().impl_;
    if (client.transport == nullptr) {
        return;
    }
    client.transport->poll_events([&](const TransportEvent& event) {
        switch (event.type) {
        case TransportEventType::Connected:
            ecs::EventWriter<NetPeerConnectedEvent>(world).send(NetPeerConnectedEvent{.peer_id = event.peer_id});
            break;
        case TransportEventType::Disconnected:
            forget_server(world, client);
            ecs::EventWriter<NetPeerDisconnectedEvent>(world).send(
                    NetPeerDisconnectedEvent{.peer_id = event.peer_id, .reason = event.reason});
            break;
        case TransportEventType::DataReceived: {
            ByteReader reader(event.payload);
            const auto id = reader.read_u16();
            if (!id.has_value()) {
                break;
            }
            if (*id == kWelcomeMessageId) {
                receive_welcome(world, client, reader);
            } else if (*id == kSnapshotMessageId) {
                receive_snapshot(world, client, reader);
            } else if (*id < kEngineMessageFirst || *id > kEngineMessageLast) {
                client.dispatcher.dispatch(event.peer_id, event.payload);
            }
            break;
        }
        }
    });
}

void run_net_client_interpolate(ecs::World& world) {
    NetClient::Impl& client = *world.ctx<NetClient>().impl_;
    if (!client.welcomed) {
        return;
    }
    if (const Time* time = world.try_ctx<Time>()) {
        client.clock.advance(static_cast<double>(time->delta_time));
    }
    if (!client.clock.started()) {
        return;
    }
    const double render_tick =
            (client.clock.now() - static_cast<double>(client.config.interpolation_delay)) / client.tick_seconds;
    const SnapshotState* from = nullptr;
    const SnapshotState* to = nullptr;
    float t = 0.0f;
    if (!client.buffer.sample(render_tick, from, to, t)) {
        return;
    }

    const ReplicationRegistry& registry = world.ctx<ReplicationRegistry>();
    auto& map = world.ctx<NetEntityMap>();

    // 1. Spawn: every entity of `from` exists. An entity destroyed under us (a cascade, the game) is re-made.
    std::vector<std::pair<ecs::Entity, NetId>> spawned;
    for (const EntitySnapshot& record : from->entities) {
        auto it = client.live.find(record.id);
        if (it != client.live.end() && !world.valid(it->second.entity)) {
            map.unbind(record.id);
            client.live.erase(it);
            it = client.live.end();
        }
        if (it == client.live.end()) {
            const ecs::Entity entity = world.create();
            map.unbind(record.id);
            bind_net_entity(world, entity, record.id, record.owner);
            client.live.emplace(record.id, NetClient::Impl::Live{.entity = entity});
            spawned.emplace_back(entity, record.id);
        } else if (auto* identity = world.try_get<NetIdentity>(it->second.entity)) {
            identity->owner = record.owner;
        }
    }

    // 2. Components, slot by slot, after every entity exists so references (Parent) resolve.
    for (const EntitySnapshot& record : from->entities) {
        NetClient::Impl::Live& live = client.live.at(record.id);
        const EntitySnapshot* next = to != nullptr ? to->find(record.id) : nullptr;
        for (std::size_t slot = 0; slot < registry.size(); ++slot) {
            const auto& entry = registry.entry(slot);
            const std::uint32_t bit = std::uint32_t{1} << slot;
            if (record.has(slot)) {
                if (next != nullptr && next->has(slot) && entry.apply_interpolated) {
                    (void)entry.apply_interpolated(
                            world, live.entity, record.component(slot), next->component(slot), t);
                } else {
                    (void)entry.apply(world, live.entity, record.component(slot));
                }
            } else if ((live.applied & bit) != 0) {
                entry.remove(world, live.entity);
            }
        }
        live.applied = record.mask;
    }

    // 3. Despawn last, so a child reparented away in this same snapshot is not taken by its old parent.
    std::vector<std::pair<NetId, ecs::Entity>> gone;
    for (const auto& [id, live] : client.live) {
        if (from->find(id) == nullptr) {
            gone.emplace_back(id, live.entity);
        }
    }
    for (const auto& [id, entity] : gone) {
        client.live.erase(id);
        despawn(world, id, entity);
    }

    for (const auto& [entity, id] : spawned) {
        ecs::EventWriter<NetEntitySpawnedEvent>(world).send(NetEntitySpawnedEvent{.entity = entity, .net_id = id});
    }
}

} // namespace engine::net
