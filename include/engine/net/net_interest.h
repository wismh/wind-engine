#pragma once

// docs/tech/modules/Net.md#interest

#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>
#include <engine/net/network_transport.h>

#include <functional>

namespace engine::net {

// Which entities each peer is told about, and how they compete for a full snapshot. The default replicates everything
// to everyone. An entity a peer owns, and one with `NetAlwaysRelevant`, is relevant whatever this says.
struct NetInterest {
    // Side of a cell of the spatial grid, in world units. 0 or less: no grid.
    float cell_size{0.0f};
    // An entity enters when its root is within this many cells of one of the peer's focuses, on every axis.
    int radius_cells{4};
    // An entity already relevant stays this many cells further, so one on the edge does not flap.
    int leave_margin_cells{1};
    // Filter over what the grid lets through (over everything, with no grid). Runs once per candidate and peer per
    // send, from `run_net_server_send`, so it should be cheap and must not change the world. Empty: all pass.
    std::function<bool(const ecs::World&, ecs::Entity, PeerId)> relevant{};
    // Weight of an entity for a peer, clamped to 0.01..1, asked only when a snapshot does not fit everything. Empty:
    // 1 / (1 + cells to the nearest focus), or 1 with no grid.
    std::function<float(const ecs::World&, ecs::Entity, PeerId)> priority{};
};

} // namespace engine::net
