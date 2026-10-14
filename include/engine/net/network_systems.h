#pragma once

#include <engine/ecs/world.h>
#include <engine/net/message_dispatcher.h>
#include <engine/net/network_transport.h>

#include <vector>

namespace engine::net {

inline constexpr MessageId kSnapshotMessageId = 0x0100;

void run_network_receive(ecs::World& world, INetworkTransport& transport, MessageDispatcher& dispatcher);

void run_network_server_replication(ecs::World& world, INetworkTransport& transport,
    const std::vector<PeerId>& clients);

void run_network_interpolation(ecs::World& world, float dt);

void setup_client_snapshot_receiver(ecs::World& world, MessageDispatcher& dispatcher);

} // namespace engine::net
