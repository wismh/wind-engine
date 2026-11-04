#pragma once

// docs/tech/modules/Net.md

#include <engine/ecs/world.h>
#include <engine/net/message_dispatcher.h>
#include <engine/net/net_client_config.h>
#include <engine/net/net_tick.h>
#include <engine/net/network_identity.h>
#include <engine/net/network_transport.h>
#include <engine/net/prediction_stats.h>

#include <memory>
#include <optional>
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

    // With a prediction channel (`register_prediction`): the server tick this client's fixed step simulates,
    // ahead of the server. `kNoTick` before the first snapshot and without a channel.
    [[nodiscard]] Tick predicted_tick() const noexcept;
    // True while reconciliation replays inputs through `PredictionDesc::step`.
    [[nodiscard]] bool replaying() const noexcept;
    // Whether the entity `id` is predicted here: this client owns it in the newest snapshot.
    [[nodiscard]] bool is_predicted(NetId id) const;
    // The server's newest report of how many ticks early this client's inputs arrive. Nullopt before one.
    [[nodiscard]] std::optional<int> input_slack() const noexcept;
    // The newest tick the server ran with this client's own input, as of the newest snapshot.
    [[nodiscard]] Tick server_input_tick() const noexcept;
    [[nodiscard]] const PredictionStats& prediction_stats() const noexcept;

    struct Impl;

private:
    friend void install_net_client(ecs::World& world, INetworkTransport& transport, NetClientConfig config);
    friend void run_net_client_receive(ecs::World& world);
    friend void run_net_client_interpolate(ecs::World& world);
    friend void run_net_client_predict(ecs::World& world);
    friend void run_net_client_send_input(ecs::World& world);

    std::unique_ptr<Impl> impl_;
};

// Points `world.ctx<NetClient>()` at `transport` (which the game owns and keeps alive while the world runs)
// and registers `run_net_client_receive`, then `run_net_client_interpolate`, on `Frame`/`NetReceive`,
// `run_net_client_predict` on `Fixed`/`NetReceive` and `run_net_client_send_input` on `Fixed`/`NetSend`.
// A second call only replaces the transport and config.
void install_net_client(ecs::World& world, INetworkTransport& transport, NetClientConfig config = {});

// Polls the transport: Welcome, snapshots (decoded into the buffer and acknowledged), and other messages to
// `dispatcher()`. A lost connection despawns every replicated entity.
void run_net_client_receive(ecs::World& world);

// Advances the clock by `Time::delta_time` and puts the world at `render_tick`: spawns the entities of the
// snapshot at or before it, applies every replicated component (interpolated toward the next snapshot when
// the component interpolates) except on predicted entities, then despawns the entities that snapshot no
// longer has.
void run_net_client_interpolate(ecs::World& world);

// With a prediction channel: starts or keeps the predicted tick, reconciles each predicted entity with the
// newest snapshot (replaying on a mismatch), advances the predicted tick, then collects each predicted
// entity's input and steps it. Sets `Time::fixed_rate` to keep the inputs `input_buffer_ticks` early.
void run_net_client_predict(ecs::World& world);

// With a prediction channel: records each predicted entity's state at the end of the tick and sends the last
// `input_redundancy` inputs of each.
void run_net_client_send_input(ecs::World& world);


} // namespace engine::net
