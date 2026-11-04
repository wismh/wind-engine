#include <engine/net/net_client.h>

#include "net/input_lead.h"
#include "net/input_message.h"
#include "net/net_schema.h"
#include "net/payload_assembler.h"
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
#include <engine/net/net_prediction.h>
#include <engine/net/network_identity.h>
#include <engine/net/replication_registry.h>

#include <algorithm>
#include <cmath>
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
        // Owned by this client in the newest snapshot, with a prediction channel: interpolation skips it.
        bool predicted{false};
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
    PayloadAssembler payloads{};
    // A malformed payload stream ended this connection; what is still in the same poll is ignored.
    bool payloads_refused{false};

    Tick predicted{kNoTick};
    bool replaying{false};
    InputLead lead{};
    std::optional<int> input_slack{};
    Tick server_input_tick{kNoTick};
    PredictionStats stats{};
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

Tick NetClient::predicted_tick() const noexcept {
    return impl_->predicted;
}

bool NetClient::replaying() const noexcept {
    return impl_->replaying;
}

bool NetClient::is_predicted(NetId id) const {
    const auto it = impl_->live.find(id);
    return it != impl_->live.end() && it->second.predicted;
}

std::optional<int> NetClient::input_slack() const noexcept {
    return impl_->input_slack;
}

Tick NetClient::server_input_tick() const noexcept {
    return impl_->server_input_tick;
}

const PredictionStats& NetClient::prediction_stats() const noexcept {
    return impl_->stats;
}

void install_net_client(ecs::World& world, INetworkTransport& transport, NetClientConfig config) {
    NetClient::Impl& client = *world.ctx<NetClient>().impl_;
    client.transport = &transport;
    client.config = config;
    client.payloads.set_limit(config.max_payload_bytes);
    (void)world.ctx<ReplicationRegistry>();
    (void)world.ctx<NetEntityMap>();
    if (client.systems_installed) {
        return;
    }
    client.systems_installed = true;
    world.add_system(ecs::Schedule::Frame, ecs::Phase::NetReceive, [](ecs::World& w) { run_net_client_receive(w); });
    world.add_system(
            ecs::Schedule::Frame, ecs::Phase::NetReceive, [](ecs::World& w) { run_net_client_interpolate(w); });
    world.add_system(ecs::Schedule::Fixed, ecs::Phase::NetReceive, [](ecs::World& w) { run_net_client_predict(w); });
    world.add_system(ecs::Schedule::Fixed, ecs::Phase::NetSend, [](ecs::World& w) { run_net_client_send_input(w); });
}

namespace {

PredictionChannel* prediction_channel(ecs::World& world) {
    NetPrediction* prediction = world.try_ctx<NetPrediction>();
    return prediction != nullptr ? prediction->channel.get() : nullptr;
}

// Stops predicting: the fixed step runs at its normal rate again.
void stop_prediction(ecs::World& world, NetClient::Impl& client) {
    if (PredictionChannel* channel = prediction_channel(world)) {
        channel->clear();
    }
    for (auto& [id, live] : client.live) {
        live.predicted = false;
    }
    client.predicted = kNoTick;
    client.replaying = false;
    client.lead.reset();
    client.input_slack.reset();
    client.server_input_tick = kNoTick;
    client.stats = PredictionStats{};
    world.ctx<Time>().fixed_rate = 1.0f;
}

// Puts every slot of `record` on the entity and removes the slots it lost.
void apply_record(ecs::World& world, const ReplicationRegistry& registry, NetClient::Impl::Live& live,
        const EntitySnapshot& record) {
    for (std::size_t slot = 0; slot < registry.size(); ++slot) {
        const auto& entry = registry.entry(slot);
        if (record.has(slot)) {
            (void)entry.apply(world, live.entity, record.component(slot));
        } else if ((live.applied & (std::uint32_t{1} << slot)) != 0) {
            entry.remove(world, live.entity);
        }
    }
    live.applied = record.mask;
}

void despawn(ecs::World& world, NetId id, ecs::Entity entity) {
    ecs::EventWriter<NetEntityDespawnedEvent>(world).send(NetEntityDespawnedEvent{.entity = entity, .net_id = id});
    world.ctx<NetEntityMap>().unbind(id);
    if (world.valid(entity)) {
        world.destroy(entity);
    }
}

void forget_server(ecs::World& world, NetClient::Impl& client) {
    stop_prediction(world, client);
    for (const auto& [id, live] : client.live) {
        despawn(world, id, live.entity);
    }
    client.live.clear();
    client.buffer.clear();
    client.clock.reset();
    client.payloads.clear();
    client.payloads_refused = false;
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
    if (*schema != welcome_schema_hash(world)) {
        log::error("net: the server replicates a different component list or prediction (register_replicated "
                   "and register_prediction must match on both sides); disconnecting");
        client.transport->disconnect(kServerPeerId);
        return;
    }
    client.welcomed = true;
    client.local_peer = *peer;
    client.tick_seconds = 1.0 / static_cast<double>(*tick_hz);
}

// The server's report on this client's inputs, from the newest snapshot. Steers the predicted tick.
void receive_input_report(ecs::World& world, NetClient::Impl& client, Tick tick, const SnapshotPeerHeader& peer) {
    client.server_input_tick = peer.input_tick;
    if (peer.input_slack == kNoInputSlack) {
        return;
    }
    client.input_slack = peer.input_slack;
    PredictionChannel* channel = prediction_channel(world);
    const bool predicting = std::any_of(
            client.live.begin(), client.live.end(), [](const auto& entry) { return entry.second.predicted; });
    if (channel == nullptr || client.predicted == kNoTick || !predicting) {
        return;
    }
    const int jump = client.lead.observe(tick, peer.input_slack, client.config.input_buffer_ticks, client.predicted);
    if (jump > 0) {
        // Inputs reach the server too late to be used: jump ahead and predict again from the next server state.
        client.predicted += static_cast<Tick>(jump);
        channel->clear();
        ++client.stats.snaps;
    }
}

void receive_snapshot(ecs::World& world, NetClient::Impl& client, ByteReader& reader) {
    if (!client.welcomed) {
        return;
    }
    Tick tick = kNoTick;
    SnapshotPeerHeader peer;
    if (!read_snapshot_header(reader, tick, peer)) {
        return;
    }
    if (client.buffer.find(tick) == nullptr) {
        auto delta = decode_snapshot(reader, tick, world.ctx<ReplicationRegistry>().size());
        if (!delta.has_value() || !client.buffer.insert(std::move(*delta))) {
            return;
        }
        if (tick > client.newest) {
            client.newest = tick;
            client.clock.observe(static_cast<double>(tick) * client.tick_seconds);
            receive_input_report(world, client, tick, peer);
        }
    }
    ByteWriter ack;
    ack.write_u32(tick);
    MessageDispatcher::send(
            *client.transport, kServerPeerId, kSnapshotAckMessageId, ack.bytes(), DeliveryMode::Unreliable);
}

void receive_payload_message(NetClient::Impl& client, MessageId id, ByteReader& reader) {
    if (!client.welcomed || client.payloads_refused) {
        return;
    }
    PayloadAssembler::Result result = id == kPayloadBeginMessageId      ? client.payloads.begin(reader)
                                      : id == kPayloadFragmentMessageId ? client.payloads.fragment(reader)
                                                                        : client.payloads.cancel(reader);
    if (result.error.has_value()) {
        log::error("net: malformed payload from the server: " + *result.error + "; disconnecting");
        client.payloads_refused = true;
        client.transport->disconnect(kServerPeerId);
        return;
    }
    if (result.done.has_value()) {
        (void)client.dispatcher.dispatch(kServerPeerId, result.done->message);
        ByteWriter ack;
        ack.write_u32(result.done->id);
        MessageDispatcher::send(
                *client.transport, kServerPeerId, kPayloadAckMessageId, ack.bytes(), DeliveryMode::ReliableOrdered);
    }
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
            } else if (*id == kPayloadBeginMessageId || *id == kPayloadFragmentMessageId ||
                       *id == kPayloadCancelMessageId) {
                receive_payload_message(client, *id, reader);
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

    // 2. Components, slot by slot, after every entity exists so references (Parent) resolve. A predicted entity
    // takes its components from reconciliation instead.
    for (const EntitySnapshot& record : from->entities) {
        NetClient::Impl::Live& live = client.live.at(record.id);
        if (live.predicted) {
            continue;
        }
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
    PredictionChannel* channel = prediction_channel(world);
    for (const auto& [id, entity] : gone) {
        client.live.erase(id);
        if (channel != nullptr) {
            channel->forget(id);
        }
        despawn(world, id, entity);
    }


    for (const auto& [entity, id] : spawned) {
        ecs::EventWriter<NetEntitySpawnedEvent>(world).send(NetEntitySpawnedEvent{.entity = entity, .net_id = id});
    }
}

void run_net_client_predict(ecs::World& world) {
    NetClient::Impl& client = *world.ctx<NetClient>().impl_;
    PredictionChannel* channel = prediction_channel(world);
    if (channel == nullptr || !client.welcomed || client.newest == kNoTick) {
        return;
    }
    const SnapshotState* newest = client.buffer.find(client.newest);
    if (newest == nullptr) {
        return;
    }
    if (client.predicted == kNoTick) {
        // Far enough ahead of the newest snapshot that inputs reach the server before their tick: a round trip
        // plus the buffer. The server's slack reports correct the guess.
        const float rtt_ms = client.transport != nullptr ? client.transport->rtt_ms(kServerPeerId) : 0.0f;
        const double rtt_ticks = rtt_ms > 0.0f ? std::ceil(rtt_ms / 1000.0 / client.tick_seconds) : 0.0;
        client.predicted = client.newest + static_cast<Tick>(rtt_ticks) + client.config.input_buffer_ticks + 1;
        client.lead.reset();
    }

    // Predicted: owned by this client in the newest snapshot.
    bool any = false;
    for (auto& [id, live] : client.live) {
        const EntitySnapshot* record = newest->find(id);
        const bool own = record != nullptr && record->owner == client.local_peer && world.valid(live.entity);
        if (own) {
            channel->begin(id, live.entity);
            live.predicted = true;
            any = true;
        } else if (live.predicted) {
            channel->forget(id);
            live.predicted = false;
        }
    }

    const ReplicationRegistry& registry = world.ctx<ReplicationRegistry>();
    for (auto& [id, live] : client.live) {
        if (!live.predicted) {
            continue;
        }
        const EntitySnapshot& record = *newest->find(id);
        const auto outcome = channel->reconcile(
                world, id, client.newest, client.predicted,
                [&world, &registry, &live, &record] { apply_record(world, registry, live, record); },
                client.replaying);
        switch (outcome) {
        case PredictionChannel::Outcome::Matched:
            ++client.stats.matched;
            break;
        case PredictionChannel::Outcome::Replayed:
            ++client.stats.replays;
            client.stats.replayed_ticks += client.predicted - client.newest;
            break;
        case PredictionChannel::Outcome::Snapped:
            ++client.stats.snaps;
            break;
        case PredictionChannel::Outcome::Skipped:
        case PredictionChannel::Outcome::Accepted:
            break;
        }
    }

    ++client.predicted;
    for (const auto& [id, live] : client.live) {
        if (live.predicted) {
            channel->predict(world, id, client.predicted);
        }
    }

    if (!any) {
        client.lead.reset();
    }
    world.ctx<Time>().fixed_rate = client.lead.rate();
    client.lead.advance(client.predicted);
}

void run_net_client_send_input(ecs::World& world) {
    NetClient::Impl& client = *world.ctx<NetClient>().impl_;
    PredictionChannel* channel = prediction_channel(world);
    if (channel == nullptr || client.transport == nullptr || client.predicted == kNoTick) {
        return;
    }
    const std::size_t redundancy = std::clamp<std::size_t>(client.config.input_redundancy, 1, kMaxInputsPerMessage);
    InputMessage message{.newest = client.predicted};
    std::vector<std::vector<std::vector<std::uint8_t>>> encoded;
    encoded.reserve(kMaxInputEntities);
    for (const auto& [id, live] : client.live) {
        if (!live.predicted) {
            continue;
        }
        channel->record(world, id, client.predicted);
        if (encoded.size() == kMaxInputEntities) {
            continue;
        }
        auto inputs = channel->inputs(id, client.predicted, redundancy);
        if (inputs.empty()) {
            continue;
        }
        encoded.push_back(std::move(inputs));
        InputMessage::Entity entity{.id = id};
        for (const auto& input : encoded.back()) {
            entity.inputs.emplace_back(input);
        }
        message.entities.push_back(std::move(entity));
    }
    if (message.entities.empty()) {
        return;
    }
    const auto bytes = message.encode();
    client.transport->send(kServerPeerId, bytes, DeliveryMode::UnreliableImmediate);
}

} // namespace engine::net

