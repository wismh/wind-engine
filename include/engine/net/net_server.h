#pragma once

// docs/tech/modules/Net.md

#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>
#include <engine/net/message_dispatcher.h>
#include <engine/net/net_input_stats.h>
#include <engine/net/net_payload_id.h>
#include <engine/net/net_payload_options.h>
#include <engine/net/net_payload_progress.h>
#include <engine/net/net_peer_stats.h>
#include <engine/net/net_server_config.h>
#include <engine/net/net_tick.h>
#include <engine/net/network_identity.h>
#include <engine/net/network_transport.h>

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace engine::net {

// The server side of a world: tick counter, connected peers, and what each peer has acknowledged.
// Lives in `world.ctx<NetServer>()`; `install_net_server` connects it to a transport and registers its systems.
class NetServer {
public:
    NetServer();
    ~NetServer();
    NetServer(const NetServer&) = delete;
    NetServer& operator=(const NetServer&) = delete;

    // The tick being simulated: `run_net_server_receive` advances it at the start of each fixed step, and the
    // snapshot `run_net_server_send` takes at the end of that step carries it. `kNoTick` before the first step.
    [[nodiscard]] Tick tick() const noexcept;
    [[nodiscard]] const NetServerConfig& config() const noexcept;
    // Handlers for the game's own messages. Engine message ids (`net_messages.h`) never reach it.
    [[nodiscard]] MessageDispatcher& dispatcher() noexcept;
    // Connected clients, ascending.
    [[nodiscard]] std::vector<PeerId> peers() const;
    // The newest snapshot `peer` acknowledged, nullopt for none or an unknown peer.
    [[nodiscard]] std::optional<Tick> acked_tick(PeerId peer) const;
    // What happened to `peer`'s inputs; nullopt for an unknown peer.
    [[nodiscard]] std::optional<NetInputStats> input_stats(PeerId peer) const;
    // The newest tick at which `peer`'s own input (not a repeat) was applied; nullopt for none or an unknown peer.
    [[nodiscard]] std::optional<Tick> input_tick(PeerId peer) const;
    // Replaces the extra points `peer` looks from (a spectator camera, a map marker). The root positions of what it
    // owns are always focuses too. Only matters with `NetInterest::cell_size`.
    void set_peer_focus(PeerId peer, std::span<const glm::vec3> focuses);
    // Nullopt for an unknown peer.
    [[nodiscard]] std::optional<NetPeerStats> peer_stats(PeerId peer) const;

    // Queues a reliable message of any size for `peer`, as `message` (a game id, outside the engine's range), to be
    // handed to the client's `dispatcher()` once whole. It goes a fragment at a time from the bytes the snapshots leave
    // in the peer's budget, highest priority first. Payloads may finish in any order, relative to each other and to
    // messages sent straight through the transport. `kNoPayload` for an unknown peer, an engine message id, or more
    // than 4 GiB. `NetPayloadDeliveredEvent` says when the client has handled it. A disconnect drops it, with no event.
    PayloadId send_payload(PeerId peer, MessageId message, std::span<const std::uint8_t> bytes,
            NetPayloadOptions options = {});
    // The same, sharing `bytes` instead of copying them: one chunk for many peers holds one buffer.
    PayloadId send_payload(PeerId peer, MessageId message, std::shared_ptr<const std::vector<std::uint8_t>> bytes,
            NetPayloadOptions options = {});
    // False for an unknown id. No delivered event follows, though a client that already holds the last byte may
    // still run its handler.
    bool cancel_payload(PayloadId id);
    // Takes effect at the next fragment. Keeps the age it has.
    bool set_payload_priority(PayloadId id, std::int32_t priority);
    // Nullopt once delivered or cancelled, for an unknown id, and after the peer left.
    [[nodiscard]] std::optional<NetPayloadProgress> payload_progress(PayloadId id) const;
    // Bytes queued for `peer` not yet handed to the transport, so a game can pace what it queues.
    [[nodiscard]] std::size_t queued_payload_bytes(PeerId peer) const;

    struct Impl;

private:
    friend void install_net_server(ecs::World& world, INetworkTransport& transport, NetServerConfig config);
    friend void run_net_server_receive(ecs::World& world);
    friend void run_net_server_send(ecs::World& world);
    friend NetId replicate(ecs::World& world, ecs::Entity entity, PeerId owner);

    std::unique_ptr<Impl> impl_;
};

// Points `world.ctx<NetServer>()` at `transport` (which the game owns and keeps alive while the world runs)
// and registers `run_net_server_receive` on `Fixed`/`NetReceive` and `run_net_server_send` on `Fixed`/`NetSend`.
// A second call only replaces the transport and config.
void install_net_server(ecs::World& world, INetworkTransport& transport, NetServerConfig config = {});

// Polls the transport: peers join (and get Welcome) or leave, acks are recorded, inputs are buffered, payload acks
// become `NetPayloadDeliveredEvent`, other messages go to `dispatcher()`. Then advances the tick and, with a prediction channel (`register_prediction`),
// steps every entity a connected client owns with that client's input for the tick (or its last one again).
void run_net_server_receive(ecs::World& world);

// Unbinds destroyed entities from `NetEntityMap`. On every `snapshot_interval`-th tick, captures the replicated state
// once and sends each peer a snapshot of what is relevant to it and differs from what it acknowledged, within its
// budget. Then, every tick, sends each peer's queued payloads from what is left of the budget.
void run_net_server_send(ecs::World& world);

// Gives `entity` a fresh `NetId` from the server and binds it, so the next snapshot carries it. A client `owner`
// controls the entity: its inputs drive it here, and with a prediction channel that client predicts it.
NetId replicate(ecs::World& world, ecs::Entity entity, PeerId owner = kServerPeerId);

} // namespace engine::net
