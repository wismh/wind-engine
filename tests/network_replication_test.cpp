#include <gtest/gtest.h>

#include <engine/ecs/events.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/net/loopback_bus.h>
#include <engine/net/loopback_transport.h>
#include <engine/net/message_dispatcher.h>
#include <engine/net/network_events.h>
#include <engine/net/network_identity.h>
#include <engine/net/network_systems.h>
#include <engine/net/replicated_transform.h>

namespace engine::net {

TEST(NetworkReplication, NetReceiveSystemGeneratesEcsEvents) {
    LoopbackBus bus;
    LoopbackTransport server(bus);
    LoopbackTransport client(bus);

    ASSERT_TRUE(server.start_server(8888));
    ASSERT_TRUE(client.connect("127.0.0.1", 8888));

    ecs::World server_world;
    MessageDispatcher server_dispatcher;

    run_network_receive(server_world, server, server_dispatcher);

    // Advance events so they are visible to readers
    server_world.flush_events();

    bool saw_connect_event = false;
    PeerId client_peer_id = kInvalidPeerId;
    ecs::EventReader<NetPeerConnectedEvent> connect_reader(server_world);
    for (const auto& ev : connect_reader) {
        saw_connect_event = true;
        client_peer_id = ev.peer_id;
    }

    EXPECT_TRUE(saw_connect_event);
    EXPECT_NE(client_peer_id, kInvalidPeerId);

    // Client disconnects
    client.disconnect(kServerPeerId);
    run_network_receive(server_world, server, server_dispatcher);
    server_world.flush_events();

    bool saw_disconnect_event = false;
    ecs::EventReader<NetPeerDisconnectedEvent> disconnect_reader(server_world);
    for (const auto& ev : disconnect_reader) {
        if (ev.peer_id == client_peer_id) {
            saw_disconnect_event = true;
            EXPECT_EQ(ev.reason, DisconnectReason::ClosedByPeer);
        }
    }
    EXPECT_TRUE(saw_disconnect_event);
}

TEST(NetworkReplication, ServerReplicationSyncsClientTransformAndInterpolates) {
    LoopbackBus bus;
    LoopbackTransport server(bus);
    LoopbackTransport client(bus);

    ASSERT_TRUE(server.start_server(8889));
    ASSERT_TRUE(client.connect("127.0.0.1", 8889));

    ecs::World server_world;
    ecs::World client_world;
    MessageDispatcher server_dispatcher;
    MessageDispatcher client_dispatcher;

    setup_client_snapshot_receiver(client_world, client_dispatcher);

    // Establish connection
    PeerId client_peer_id = kInvalidPeerId;
    server.poll_events([&](const TransportEvent& ev) {
        if (ev.type == TransportEventType::Connected) {
            client_peer_id = ev.peer_id;
        }
    });
    client.poll_events([](const TransportEvent&) {});
    ASSERT_NE(client_peer_id, kInvalidPeerId);

    // Spawn server entity
    const auto server_entity = server_world.create();
    server_world.emplace<NetIdentity>(server_entity, NetIdentity{.id = 42, .owner_peer_id = client_peer_id});
    server_world.emplace<Transform>(server_entity, Transform{.position = glm::vec3(100.0f, 50.0f, 0.0f)});

    // Spawn client entity with same NetId
    const auto client_entity = client_world.create();
    client_world.emplace<NetIdentity>(client_entity, NetIdentity{.id = 42, .owner_peer_id = client_peer_id});
    client_world.emplace<Transform>(client_entity, Transform{.position = glm::vec3(0.0f, 0.0f, 0.0f)});

    // Server sends snapshot replication
    const std::vector<PeerId> connected_clients = {client_peer_id};
    run_network_server_replication(server_world, server, connected_clients);

    // Client receives snapshot
    run_network_receive(client_world, client, client_dispatcher);

    // Verify ReplicatedTransform target is set on client
    const auto* repl = client_world.try_get<ReplicatedTransform>(client_entity);
    ASSERT_NE(repl, nullptr);
    EXPECT_FLOAT_EQ(repl->target_position.x, 100.0f);
    EXPECT_FLOAT_EQ(repl->target_position.y, 50.0f);

    // Run interpolation over several frames
    for (int i = 0; i < 30; ++i) {
        run_network_interpolation(client_world, 1.0f / 60.0f);
    }

    const auto* client_tr = client_world.try_get<Transform>(client_entity);
    ASSERT_NE(client_tr, nullptr);
    // After 0.5s at lerp_speed=15, client position should be very close to target
    EXPECT_NEAR(client_tr->position.x, 100.0f, 1.0f);
    EXPECT_NEAR(client_tr->position.y, 50.0f, 1.0f);
}

} // namespace engine::net
