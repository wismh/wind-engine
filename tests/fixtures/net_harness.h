#pragma once

// A server world and client worlds joined by SimulatedLinks. `step` is one 60 Hz frame: the server runs one fixed
// step, the links advance 1/60 s, then each client runs the fixed steps its own clock gives for 1/60 s (more or fewer
// while prediction dilates `Time::fixed_rate`) and one frame. Spawn and despawn events of each client are collected
// per step. `client` is the first client; `add_client` adds more.

#include "simulated_link.h"

#include <engine/core/application_state.h>
#include <engine/core/fixed_step.h>
#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/net/byte_stream.h>
#include <engine/net/loopback_bus.h>
#include <engine/net/net_client.h>
#include <engine/net/net_entity_despawned_event.h>
#include <engine/net/net_entity_map.h>
#include <engine/net/net_entity_spawned_event.h>
#include <engine/net/net_messages.h>
#include <engine/net/net_server.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace engine::net::testing {

// The header and record count of a Snapshot message, as sent.
struct WireSnapshot {
    Tick tick{kNoTick};
    Tick input_tick{kNoTick};
    std::int8_t input_slack{kNoInputSlack};
    std::uint16_t removed{0};
    std::uint16_t entities{0};
    // The id of each record, as written.
    std::vector<NetId> ids{};
    std::vector<NetId> removed_ids{};
    std::size_t size{0};
    PeerId target{kInvalidPeerId};
    bool dropped{false};
};

[[nodiscard]] inline std::optional<std::uint16_t> message_id(std::span<const std::uint8_t> bytes) {
    ByteReader reader(bytes);
    return reader.read_u16();
}

[[nodiscard]] inline std::optional<WireSnapshot> parse_snapshot(std::span<const std::uint8_t> bytes) {
    ByteReader reader(bytes);
    if (reader.read_u16() != kSnapshotMessageId) {
        return std::nullopt;
    }
    WireSnapshot out;
    out.size = bytes.size();
    const auto tick = reader.read_u32();
    const auto input_tick = reader.read_u32();
    const auto input_slack = reader.read_i8();
    const auto removed = reader.read_u16();
    if (!tick || !input_tick || !input_slack || !removed) {
        return std::nullopt;
    }
    for (std::uint16_t i = 0; i < *removed; ++i) {
        const auto id = reader.read_u32();
        if (!id) {
            return std::nullopt;
        }
        out.removed_ids.push_back(*id);
    }
    const auto entities = reader.read_u16();
    if (!entities) {
        return std::nullopt;
    }
    out.tick = *tick;
    out.input_tick = *input_tick;
    out.input_slack = *input_slack;
    out.removed = *removed;
    out.entities = *entities;
    for (std::uint16_t i = 0; i < *entities; ++i) {
        const auto id = reader.read_u32();
        const auto flags = reader.read_u8();
        if (!id || !flags || ((*flags & 1u) != 0 && !reader.skip(8))) {
            return std::nullopt;
        }
        const auto changed = reader.read_u32();
        if (!changed || ((*flags & 2u) != 0 && !reader.skip(4))) {
            return std::nullopt;
        }
        for (std::uint32_t bits = *changed; bits != 0; bits &= bits - 1u) {
            const auto size = reader.read_u16();
            if (!size || !reader.skip(*size)) {
                return std::nullopt;
            }
        }
        out.ids.push_back(*id);
    }
    return out;
}

[[nodiscard]] inline std::vector<WireSnapshot> snapshots_sent(const SimulatedLink& link) {
    std::vector<WireSnapshot> out;
    for (const auto& sent : link.sent()) {
        if (auto snapshot = parse_snapshot(sent.bytes)) {
            snapshot->dropped = sent.dropped;
            snapshot->target = sent.target;
            out.push_back(std::move(*snapshot));
        }
    }
    return out;
}

// One client beside the first: its own link, world, clock, and collected events.
class NetClientSide {
public:
    NetClientSide(LoopbackBus& bus, NetClientConfig config)
        : link(bus)
        , config_(config) {}

    void connect(std::uint16_t port) {
        install_net_client(world, link, config_);
        link.connect("loopback", port);
    }

    void advance_link() {
        link.advance(1000.0 / 60.0);
    }

    void run() {
        world.flush_events();
        const int steps = clock_.advance(kFixed);
        for (int s = 0; s < steps; ++s) {
            world.run(ecs::Schedule::Fixed);
        }
        world.run(ecs::Schedule::Frame);
        for (const auto& event : ecs::EventReader<NetEntitySpawnedEvent>(world, spawned_cursor_)) {
            spawned.push_back(event);
        }
        for (const auto& event : ecs::EventReader<NetEntityDespawnedEvent>(world, despawned_cursor_)) {
            despawned.push_back(event);
        }
    }

    [[nodiscard]] std::optional<ecs::Entity> entity(NetId id) const {
        return find_entity(world, id);
    }

    SimulatedLink link;
    ecs::World world;
    std::vector<NetEntitySpawnedEvent> spawned;
    std::vector<NetEntityDespawnedEvent> despawned;

private:
    NetClientConfig config_;
    ApplicationState app_{};
    FixedStepClock clock_{world.ctx<Time>(), app_};
    ecs::EventCursor<NetEntitySpawnedEvent> spawned_cursor_{};
    ecs::EventCursor<NetEntityDespawnedEvent> despawned_cursor_{};
};

class NetHarness {
public:
    explicit NetHarness(std::uint16_t port, NetServerConfig server_config = {}, NetClientConfig client_config = {})
        : port_(port)
        , server_config_(std::move(server_config))
        , client_config_(client_config) {}

    // Installs both sides and connects. Register replicated components before this. Clients get their peer ids in
    // order: the first client first, then each `add_client`.
    void connect() {
        install_net_server(server, server_link, server_config_);
        install_net_client(client, client_link, client_config_);
        server_link.start_server(port_);
        client_link.connect("loopback", port_);
        connected_ = true;
        for (const auto& side : extra_clients_) {
            side->connect(port_);
        }
    }

    // Another client. Before `connect` it joins at `connect`; after it, at once.
    NetClientSide& add_client(NetClientConfig config = {}) {
        extra_clients_.push_back(std::make_unique<NetClientSide>(bus, config));
        if (connected_) {
            extra_clients_.back()->connect(port_);
        }
        return *extra_clients_.back();
    }

    void step(int frames = 1) {
        for (int i = 0; i < frames; ++i) {
            server.flush_events();
            client.flush_events();
            server.ctx<Time>().fixed_delta_time = kFixed;
            server.run(ecs::Schedule::Fixed);
            server_link.advance(1000.0 / 60.0);
            client_link.advance(1000.0 / 60.0);
            for (const auto& side : extra_clients_) {
                side->advance_link();
            }
            const int client_steps = client_clock_.advance(kFixed);
            for (int s = 0; s < client_steps; ++s) {
                client.run(ecs::Schedule::Fixed);
            }
            client.run(ecs::Schedule::Frame);
            for (const auto& event : ecs::EventReader<NetEntitySpawnedEvent>(client, spawned_cursor_)) {
                spawned.push_back(event);
            }
            for (const auto& event : ecs::EventReader<NetEntityDespawnedEvent>(client, despawned_cursor_)) {
                despawned.push_back(event);
            }
            for (const auto& side : extra_clients_) {
                side->run();
            }
        }
    }

    [[nodiscard]] Tick tick() {
        return server.ctx<NetServer>().tick();
    }

    // A server entity with `transform`, replicated.
    NetId spawn(const Transform& transform = {}) {
        const ecs::Entity entity = server.create();
        server.emplace<Transform>(entity, transform);
        return replicate(server, entity);
    }

    [[nodiscard]] ecs::Entity server_entity(NetId id) const {
        return *find_entity(server, id);
    }

    [[nodiscard]] std::optional<ecs::Entity> client_entity(NetId id) const {
        return find_entity(client, id);
    }

    LoopbackBus bus;
    SimulatedLink server_link{bus};
    SimulatedLink client_link{bus};
    ecs::World server;
    ecs::World client;
    std::vector<NetEntitySpawnedEvent> spawned;
    std::vector<NetEntityDespawnedEvent> despawned;

private:
    std::uint16_t port_;
    NetServerConfig server_config_;
    NetClientConfig client_config_;
    bool connected_{false};
    ApplicationState app_{};
    FixedStepClock client_clock_{client.ctx<Time>(), app_};
    std::vector<std::unique_ptr<NetClientSide>> extra_clients_{};

    ecs::EventCursor<NetEntitySpawnedEvent> spawned_cursor_{};
    ecs::EventCursor<NetEntityDespawnedEvent> despawned_cursor_{};
};

} // namespace engine::net::testing
