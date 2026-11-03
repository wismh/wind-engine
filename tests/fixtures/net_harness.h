#pragma once

// A server world and a client world joined by two SimulatedLinks. `step` is one 60 Hz frame: the server runs
// one fixed step, both links advance 1/60 s, the client runs one frame. Spawn and despawn events of the
// client are collected per step.

#include "fixtures/simulated_link.h"

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
#include <optional>
#include <span>
#include <vector>

namespace engine::net::testing {

// The header and record count of a Snapshot message, as sent.
struct WireSnapshot {
    Tick tick{kNoTick};
    Tick baseline{kNoTick};
    std::uint16_t removed{0};
    std::uint16_t entities{0};
    std::size_t size{0};
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
    const auto baseline = reader.read_u32();
    const auto removed = reader.read_u16();
    if (!tick || !baseline || !removed || !reader.skip(std::size_t{*removed} * 4u)) {
        return std::nullopt;
    }
    const auto entities = reader.read_u16();
    if (!entities) {
        return std::nullopt;
    }
    out.tick = *tick;
    out.baseline = *baseline;
    out.removed = *removed;
    out.entities = *entities;
    return out;
}

[[nodiscard]] inline std::vector<WireSnapshot> snapshots_sent(const SimulatedLink& link) {
    std::vector<WireSnapshot> out;
    for (const auto& sent : link.sent()) {
        if (auto snapshot = parse_snapshot(sent.bytes)) {
            snapshot->dropped = sent.dropped;
            out.push_back(*snapshot);
        }
    }
    return out;
}

class NetHarness {
public:
    explicit NetHarness(std::uint16_t port, NetServerConfig server_config = {}, NetClientConfig client_config = {})
        : port_(port)
        , server_config_(server_config)
        , client_config_(client_config) {}

    // Installs both sides and connects. Register replicated components before this.
    void connect() {
        install_net_server(server, server_link, server_config_);
        install_net_client(client, client_link, client_config_);
        server_link.start_server(port_);
        client_link.connect("loopback", port_);
    }

    void step(int frames = 1) {
        for (int i = 0; i < frames; ++i) {
            server.flush_events();
            client.flush_events();
            server.ctx<Time>().fixed_delta_time = kFixed;
            server.run(ecs::Schedule::Fixed);
            server_link.advance(1000.0 / 60.0);
            client_link.advance(1000.0 / 60.0);
            client.ctx<Time>().delta_time = kFixed;
            client.run(ecs::Schedule::Frame);
            for (const auto& event : ecs::EventReader<NetEntitySpawnedEvent>(client, spawned_cursor_)) {
                spawned.push_back(event);
            }
            for (const auto& event : ecs::EventReader<NetEntityDespawnedEvent>(client, despawned_cursor_)) {
                despawned.push_back(event);
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
    ecs::EventCursor<NetEntitySpawnedEvent> spawned_cursor_{};
    ecs::EventCursor<NetEntityDespawnedEvent> despawned_cursor_{};
};

} // namespace engine::net::testing
