#pragma once

// docs/tech/modules/Net.md

#include <engine/ecs/world.h>
#include <engine/net/message_dispatcher.h>
#include <engine/net/net_client_config.h>
#include <engine/net/net_tick.h>
#include <engine/net/network_transport.h>

#include <memory>
#include <vector>

namespace engine::net {

// The client side of a world: the snapshot buffer, the server clock estimate, and the replicated entities it
// created. Lives in `world.ctx<NetClient>()`; `install_net_client` connects it to a transport and registers
// its systems.
class NetClient {
public:
    NetClient();
    ~NetClient();
    NetClient(const NetClient&) = delete;
    NetClient& operator=(const NetClient&) = delete;

    [[nodiscard]] const NetClientConfig& config() const noexcept;
    // True between an accepted Welcome and the end of the connection.
    [[nodiscard]] bool welcomed() const noexcept;
    // This client's id on the server, from Welcome; compare it with `NetIdentity::owner`.
    [[nodiscard]] PeerId local_peer() const noexcept;
    // The newest snapshot tick decoded.
    [[nodiscard]] Tick newest_tick() const noexcept;
    // The server time, in ticks, the replicated entities show now: the estimated newest snapshot time minus
    // `interpolation_delay`. Negative before the first snapshot.
    [[nodiscard]] double render_tick() const noexcept;
    // The ticks held in the snapshot buffer, ascending.
    [[nodiscard]] std::vector<Tick> buffered_ticks() const;
    // Handlers for the game's own messages. Engine message ids (`net_messages.h`) never reach it.
    [[nodiscard]] MessageDispatcher& dispatcher() noexcept;

    struct Impl;

private:
    friend void install_net_client(ecs::World& world, INetworkTransport& transport, NetClientConfig config);
    friend void run_net_client_receive(ecs::World& world);
    friend void run_net_client_interpolate(ecs::World& world);

    std::unique_ptr<Impl> impl_;
};

// Points `world.ctx<NetClient>()` at `transport` (which the game owns and keeps alive while the world runs)
// and registers `run_net_client_receive`, then `run_net_client_interpolate`, on `Frame`/`NetReceive`.
// A second call only replaces the transport and config.
void install_net_client(ecs::World& world, INetworkTransport& transport, NetClientConfig config = {});

// Polls the transport: Welcome, snapshots (decoded into the buffer and acknowledged), and other messages to
// `dispatcher()`. A lost connection despawns every replicated entity.
void run_net_client_receive(ecs::World& world);

// Advances the clock by `Time::delta_time` and puts the world at `render_tick`: spawns the entities of the
// snapshot at or before it, applies every replicated component (interpolated toward the next snapshot when
// the component interpolates), then despawns the entities that snapshot no longer has.
void run_net_client_interpolate(ecs::World& world);

} // namespace engine::net
