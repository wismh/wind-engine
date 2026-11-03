#pragma once

// docs/tech/modules/Net.md

#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>
#include <engine/net/message_dispatcher.h>
#include <engine/net/net_server_config.h>
#include <engine/net/net_tick.h>
#include <engine/net/network_identity.h>
#include <engine/net/network_transport.h>

#include <memory>
#include <optional>
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

// Polls the transport: peers join (and get Welcome) or leave, acks are recorded, other messages go to
// `dispatcher()`. Then advances the tick.
void run_net_server_receive(ecs::World& world);

// Unbinds destroyed entities from `NetEntityMap`. On every `snapshot_interval`-th tick, captures the
// replicated state and sends each peer a delta against its acked baseline, or a full snapshot without one.
void run_net_server_send(ecs::World& world);

// Gives `entity` a fresh `NetId` from the server and binds it, so the next snapshot carries it.
NetId replicate(ecs::World& world, ecs::Entity entity, PeerId owner = kServerPeerId);

} // namespace engine::net
