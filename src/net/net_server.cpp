#include <engine/net/net_server.h>

#include "net/input_buffer.h"
#include "net/input_message.h"
#include "net/net_schema.h"
#include "net/payload_queue.h"
#include "net/peer_ghosts.h"
#include "net/relevance_grid.h"
#include "net/snapshot_codec.h"
#include "net/snapshot_state.h"

#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/transform.h>
#include <engine/log.h>
#include <engine/net/byte_stream.h>
#include <engine/net/net_always_relevant.h>
#include <engine/net/net_entity_map.h>
#include <engine/net/net_id.h>
#include <engine/net/net_messages.h>
#include <engine/net/net_payload_delivered_event.h>
#include <engine/net/net_prediction.h>
#include <engine/net/net_peer_connected_event.h>
#include <engine/net/net_peer_disconnected_event.h>
#include <engine/net/replication_registry.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::net {

struct NetServer::Impl {
    // What the relevance pass needs per captured entity, parallel to `capture.entities`.
    struct CaptureInfo {
        ecs::Entity entity{};
        glm::vec3 root_position{0.0f};
        glm::ivec3 cell{0};
        // Its root has a `Transform`: it has a place in the grid.
        bool spatial{false};
        bool always{false};
        // Index of the replicated parent; -1 for none.
        std::int32_t parent{-1};
    };

    struct Peer {
        PeerGhosts ghosts{};
        std::vector<glm::vec3> focuses{};
        PayloadQueue payloads{};
        // Bytes this peer may still be sent. Earned per tick, spent by snapshots first.
        double credit{0.0};
        NetPeerStats net_stats{};
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
    };

    INetworkTransport* transport{nullptr};
    NetServerConfig config{};
    bool systems_installed{false};
    Tick tick{kNoTick};
    MessageDispatcher dispatcher{};
    NetIdGenerator ids{};
    std::map<PeerId, Peer> peers{};

    // The state every peer's snapshot reads, serialized once per send tick.
    SnapshotState capture{};
    std::vector<CaptureInfo> capture_info{};
    RelevanceGrid grid{};

    PayloadId next_payload{kNoPayload};
    std::unordered_map<PayloadId, PeerId> payload_owner{};
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
    if (it == impl_->peers.end() || it->second.ghosts.acked() == kNoTick) {
        return std::nullopt;
    }
    return it->second.ghosts.acked();
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

void NetServer::set_peer_focus(PeerId peer, std::span<const glm::vec3> focuses) {
    const auto it = impl_->peers.find(peer);
    if (it != impl_->peers.end()) {
        it->second.focuses.assign(focuses.begin(), focuses.end());
    }
}

std::optional<NetPeerStats> NetServer::peer_stats(PeerId peer) const {
    const auto it = impl_->peers.find(peer);
    if (it == impl_->peers.end()) {
        return std::nullopt;
    }
    return it->second.net_stats;
}

PayloadId NetServer::send_payload(
        PeerId peer, MessageId message, std::span<const std::uint8_t> bytes, NetPayloadOptions options) {
    return send_payload(
            peer, message, std::make_shared<const std::vector<std::uint8_t>>(bytes.begin(), bytes.end()), options);
}

PayloadId NetServer::send_payload(PeerId peer, MessageId message, std::shared_ptr<const std::vector<std::uint8_t>> bytes,
        NetPayloadOptions options) {
    const auto it = impl_->peers.find(peer);
    if (it == impl_->peers.end() || message == kInvalidMessageId ||
            (message >= kEngineMessageFirst && message <= kEngineMessageLast) || bytes == nullptr ||
            bytes->size() > std::numeric_limits<std::uint32_t>::max()) {
        log::error("net: send_payload refused: message " + std::to_string(message) + " for peer " +
                   std::to_string(peer));
        return kNoPayload;
    }
    const PayloadId id = ++impl_->next_payload;
    it->second.payloads.push(id, message, std::move(bytes), options, impl_->tick);
    impl_->payload_owner.emplace(id, peer);
    return id;
}

bool NetServer::cancel_payload(PayloadId id) {
    const auto owner = impl_->payload_owner.find(id);
    if (owner == impl_->payload_owner.end()) {
        return false;
    }
    const auto peer = impl_->peers.find(owner->second);
    const bool cancelled = peer != impl_->peers.end() && impl_->transport != nullptr &&
                           peer->second.payloads.cancel(*impl_->transport, owner->second, id);
    impl_->payload_owner.erase(owner);
    return cancelled;
}

bool NetServer::set_payload_priority(PayloadId id, std::int32_t priority) {
    const auto owner = impl_->payload_owner.find(id);
    if (owner == impl_->payload_owner.end()) {
        return false;
    }
    const auto peer = impl_->peers.find(owner->second);
    return peer != impl_->peers.end() && peer->second.payloads.set_priority(id, priority);
}

std::optional<NetPayloadProgress> NetServer::payload_progress(PayloadId id) const {
    const auto owner = impl_->payload_owner.find(id);
    if (owner == impl_->payload_owner.end()) {
        return std::nullopt;
    }
    const auto peer = impl_->peers.find(owner->second);
    if (peer == impl_->peers.end()) {
        return std::nullopt;
    }
    return peer->second.payloads.progress(id);
}

std::size_t NetServer::queued_payload_bytes(PeerId peer) const {
    const auto it = impl_->peers.find(peer);
    return it != impl_->peers.end() ? it->second.payloads.queued_bytes() : 0u;
}

void install_net_server(ecs::World& world, INetworkTransport& transport, NetServerConfig config) {
    NetServer::Impl& server = *world.ctx<NetServer>().impl_;
    server.transport = &transport;
    server.config = std::move(config);
    if (server.config.snapshot_interval == 0) {
        server.config.snapshot_interval = 1;
    }
    server.config.snapshot_budget_bytes =
            std::clamp<std::size_t>(server.config.snapshot_budget_bytes, kMinSnapshotBudget, kMaxSnapshotBudget);
    server.config.peer_bytes_per_second = std::max<std::size_t>(server.config.peer_bytes_per_second, 1024u);
    server.config.payload_min_share = std::clamp(server.config.payload_min_share, 0.0f, 0.9f);
    server.config.payload_aging_per_second = std::max(server.config.payload_aging_per_second, 0.0f);
    server.config.interest.radius_cells = std::max(server.config.interest.radius_cells, 0);
    server.config.interest.leave_margin_cells = std::max(server.config.interest.leave_margin_cells, 0);
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

constexpr float kMinEntityWeight = 0.01f;
constexpr float kMaxEntityWeight = 1.0f;

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
    it->second.ghosts.acknowledge(*tick);
}

void receive_payload_ack(ecs::World& world, NetServer::Impl& server, PeerId peer, ByteReader& reader) {
    const auto id = reader.read_u32();
    const auto it = server.peers.find(peer);
    if (!id.has_value() || it == server.peers.end() || !it->second.payloads.acknowledge(*id)) {
        return;
    }
    server.payload_owner.erase(*id);
    ecs::EventWriter<NetPayloadDeliveredEvent>(world).send(NetPayloadDeliveredEvent{.peer = peer, .id = *id});
}

void forget_peer(NetServer::Impl& server, PeerId peer) {
    const auto it = server.peers.find(peer);
    if (it == server.peers.end()) {
        return;
    }
    for (const PayloadId id : it->second.payloads.ids()) {
        server.payload_owner.erase(id);
    }
    server.peers.erase(it);
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

// Takes the shared capture: one serialization per entity per send tick, whatever the number of peers.
void capture_world(ecs::World& world, NetServer::Impl& server) {
    server.capture = capture_snapshot(world, world.ctx<ReplicationRegistry>(), server.tick, &server.capture);

    const auto index_of = [&server](NetId id) -> std::int32_t {
        const auto& entities = server.capture.entities;
        const auto at = std::lower_bound(entities.begin(), entities.end(), id,
                [](const EntitySnapshot& entity, NetId value) { return entity.id < value; });
        return at != entities.end() && at->id == id ? static_cast<std::int32_t>(at - entities.begin()) : -1;
    };

    const NetInterest& interest = server.config.interest;
    const bool grid = interest.cell_size > 0.0f;
    server.capture_info.assign(server.capture.entities.size(), NetServer::Impl::CaptureInfo{});
    std::vector<RelevanceGrid::Item> items;
    if (grid) {
        items.reserve(server.capture.entities.size());
    }
    for (const ecs::Entity entity : world.view<NetIdentity>()) {
        const NetId id = world.get<NetIdentity>(entity).id;
        const std::int32_t index = id != kInvalidNetId ? index_of(id) : -1;
        if (index < 0) {
            continue;
        }
        NetServer::Impl::CaptureInfo& info = server.capture_info[static_cast<std::size_t>(index)];
        info.entity = entity;
        info.always = world.try_get<NetAlwaysRelevant>(entity) != nullptr;

        ecs::Entity root = entity;
        if (const auto* parent = world.try_get<Parent>(entity)) {
            if (const auto parent_id = find_net_id(world, parent->entity)) {
                info.parent = index_of(*parent_id);
            }
        }
        for (int depth = 0; depth <= ecs::kMaxHierarchyDepth; ++depth) {
            const auto* parent = world.try_get<Parent>(root);
            if (parent == nullptr || !world.valid(parent->entity)) {
                break;
            }
            root = parent->entity;
        }
        if (const auto* transform = world.try_get<Transform>(root)) {
            info.spatial = true;
            info.root_position = transform->position;
            if (grid) {
                info.cell = RelevanceGrid::cell_of(info.root_position, interest.cell_size);
                items.push_back(RelevanceGrid::Item{.cell = info.cell, .index = static_cast<std::uint32_t>(index)});
            }
        }
    }
    if (grid) {
        server.grid.rebuild(std::move(items));
    }
}

[[nodiscard]] std::vector<glm::ivec3> peer_focus_cells(const NetServer::Impl& server, const NetServer::Impl::Peer& client,
        PeerId peer) {
    std::vector<glm::ivec3> cells;
    cells.reserve(client.focuses.size() + 4);
    const float cell_size = server.config.interest.cell_size;
    for (std::size_t i = 0; i < server.capture.entities.size(); ++i) {
        if (server.capture.entities[i].owner == peer && server.capture_info[i].spatial) {
            cells.push_back(server.capture_info[i].cell);
        }
    }
    for (const glm::vec3& focus : client.focuses) {
        cells.push_back(RelevanceGrid::cell_of(focus, cell_size));
    }
    return cells;
}

[[nodiscard]] int nearest_focus(const glm::ivec3& cell, std::span<const glm::ivec3> focuses) noexcept {
    int nearest = std::numeric_limits<int>::max();
    for (const glm::ivec3& focus : focuses) {
        nearest = std::min(nearest, RelevanceGrid::distance(cell, focus));
    }
    return nearest;
}

// What `peer` is told about at this send, sorted by id like the capture.
[[nodiscard]] std::vector<RelevantEntity> relevant_for(ecs::World& world, NetServer::Impl& server,
        NetServer::Impl::Peer& client, PeerId peer, std::span<const glm::ivec3> focuses, bool predicting) {
    const NetInterest& interest = server.config.interest;
    const auto& entities = server.capture.entities;
    const bool grid = interest.cell_size > 0.0f;
    std::vector<std::uint8_t> marks(entities.size(), 0);

    // Candidates: what the grid lets through, then the filter.
    if (grid) {
        std::vector<std::uint8_t> candidates(entities.size(), 0);
        for (const glm::ivec3& focus : focuses) {
            server.grid.mark_around(focus, interest.radius_cells, candidates);
        }
        const int keep_within = interest.radius_cells + interest.leave_margin_cells;
        for (const PeerGhosts::Ghost& ghost : client.ghosts.ghosts()) {
            if (ghost.leaving) {
                continue;
            }
            const auto at = std::lower_bound(entities.begin(), entities.end(), ghost.id,
                    [](const EntitySnapshot& entity, NetId value) { return entity.id < value; });
            if (at == entities.end() || at->id != ghost.id) {
                continue;
            }
            const auto index = static_cast<std::size_t>(at - entities.begin());
            if (server.capture_info[index].spatial &&
                    nearest_focus(server.capture_info[index].cell, focuses) <= keep_within) {
                candidates[index] = 1;
            }
        }
        for (std::size_t i = 0; i < entities.size(); ++i) {
            const auto& info = server.capture_info[i];
            if ((candidates[i] != 0 || !info.spatial) &&
                    (!interest.relevant || interest.relevant(world, info.entity, peer))) {
                marks[i] = 1;
            }
        }
    } else {
        for (std::size_t i = 0; i < entities.size(); ++i) {
            if (!interest.relevant || interest.relevant(world, server.capture_info[i].entity, peer)) {
                marks[i] = 1;
            }
        }
    }

    // Whatever the filter said: what the peer owns, what is always relevant, and everything those hang from, without
    // which the client would place a child in the wrong space.
    std::vector<RelevantEntity> relevant;
    relevant.reserve(entities.size());
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (entities[i].owner == peer || server.capture_info[i].always) {
            marks[i] = 1;
        }
    }
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (marks[i] == 0) {
            continue;
        }
        std::int32_t parent = server.capture_info[i].parent;
        for (int depth = 0; parent >= 0 && depth <= ecs::kMaxHierarchyDepth; ++depth) {
            marks[static_cast<std::size_t>(parent)] = 1;
            parent = server.capture_info[static_cast<std::size_t>(parent)].parent;
        }
    }
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (marks[i] == 0) {
            continue;
        }
        const std::int32_t parent = server.capture_info[i].parent;
        relevant.push_back(RelevantEntity{.entity = &entities[i],
                .capture_index = static_cast<std::uint32_t>(i),
                .parent = parent >= 0 ? entities[static_cast<std::size_t>(parent)].id : kInvalidNetId,
                .mandatory = predicting && entities[i].owner == peer});
    }
    return relevant;
}

void send_snapshot(ecs::World& world, NetServer::Impl& server, NetServer::Impl::Peer& client, PeerId peer,
        double bytes_per_tick) {
    const NetInterest& interest = server.config.interest;
    const bool grid = interest.cell_size > 0.0f;
    const bool predicting = prediction_channel(world) != nullptr;
    const std::vector<glm::ivec3> focuses =
            grid ? peer_focus_cells(server, client, peer) : std::vector<glm::ivec3>{};
    const std::vector<RelevantEntity> relevant = relevant_for(world, server, client, peer, focuses, predicting);

    const auto weight = [&](const RelevantEntity& entity) {
        if (entity.entity->owner == peer) {
            return kMaxEntityWeight;
        }
        const auto& info = server.capture_info[entity.capture_index];
        float value = kMaxEntityWeight;
        if (interest.priority) {
            value = interest.priority(world, info.entity, peer);
        } else if (grid && info.spatial && !focuses.empty()) {
            value = 1.0f / (1.0f + static_cast<float>(nearest_focus(info.cell, focuses)));
        }
        return std::isfinite(value) ? std::clamp(value, kMinEntityWeight, kMaxEntityWeight) : kMinEntityWeight;
    };

    // Snapshots may take all of the rate but the payloads' share, and never more than one packet.
    const double share = 1.0 - static_cast<double>(server.config.payload_min_share);
    const double allowance = share * bytes_per_tick * static_cast<double>(server.config.snapshot_interval);
    const std::size_t budget = std::clamp<std::size_t>(
            static_cast<std::size_t>(allowance), kMinSnapshotBudget, server.config.snapshot_budget_bytes);

    PeerGhosts::Built built = client.ghosts.build(server.tick, take_peer_header(client), relevant, budget,
            world.ctx<ReplicationRegistry>().size(), weight);

    if (built.bytes.size() > budget && !client.warned_size) {
        client.warned_size = true;
        log::warn("net: snapshot " + std::to_string(built.bytes.size()) + " bytes for peer " + std::to_string(peer) +
                  " exceeds its budget of " + std::to_string(budget) +
                  " with records that cannot wait; it may be split and lost whole if any piece is lost");
    }
    server.transport->send(peer, built.bytes, DeliveryMode::UnreliableImmediate);
    // Never more than the snapshots' part of the rate, even for records that cannot wait: the payloads' part stays
    // theirs. Credit can go negative after a payload fragment; the rate earns it back before the next one.
    client.credit -= std::min(static_cast<double>(built.bytes.size()), allowance);
    client.net_stats.relevant = built.relevant;
    client.net_stats.deferred = built.deferred;
    client.net_stats.last_snapshot_bytes = built.bytes.size();
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
            forget_peer(server, event.peer_id);
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
            } else if (*id == kPayloadAckMessageId) {
                receive_payload_ack(world, server, event.peer_id, reader);
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
    if (server.peers.empty()) {
        return;
    }

    const bool snapshot_due = server.tick % server.config.snapshot_interval == 0;
    if (snapshot_due) {
        capture_world(world, server);
    }

    const double bytes_per_tick = static_cast<double>(server.config.peer_bytes_per_second) * static_cast<double>(kFixed);
    const double most_credit = std::max(bytes_per_tick * 2.0 * static_cast<double>(server.config.snapshot_interval),
            static_cast<double>(kPayloadFragmentBytes) * 2.0);
    const PayloadQueue::Limits limits{
            .aging_per_tick = static_cast<double>(server.config.payload_aging_per_second) * static_cast<double>(kFixed),
            .backlog_bytes = server.config.transport_backlog_bytes,
    };
    for (auto& [peer, client] : server.peers) {
        client.credit = std::min(client.credit + bytes_per_tick, most_credit);
        // Snapshots first, unconditionally; the payloads get what is left.
        if (snapshot_due) {
            send_snapshot(world, server, client, peer, bytes_per_tick);
        }
        client.net_stats.payload_bytes_sent +=
                client.payloads.pump(*server.transport, peer, server.tick, limits, client.credit);
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
