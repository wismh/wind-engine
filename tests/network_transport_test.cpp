#include <gtest/gtest.h>

#include <engine/net/loopback_bus.h>
#include <engine/net/loopback_transport.h>
#include <engine/net/network_transport.h>

#include <string>
#include <vector>

namespace engine::net {

TEST(NetworkTransport, ServerStartAndStop) {
    LoopbackBus bus;
    LoopbackTransport server(bus);

    EXPECT_FALSE(server.is_running());
    EXPECT_TRUE(server.start_server(7777));
    EXPECT_TRUE(server.is_running());
    EXPECT_TRUE(server.is_server());

    // Starting again while running should fail
    EXPECT_FALSE(server.start_server(7777));

    server.stop();
    EXPECT_FALSE(server.is_running());

    // Should be able to restart after stopping
    EXPECT_TRUE(server.start_server(7777));
    EXPECT_TRUE(server.is_running());
}

TEST(NetworkTransport, ServerDuplicatePortFails) {
    LoopbackBus bus;
    LoopbackTransport server1(bus);
    LoopbackTransport server2(bus);

    EXPECT_TRUE(server1.start_server(7777));
    EXPECT_FALSE(server2.start_server(7777));
}

TEST(NetworkTransport, ClientConnectToNonExistentServerFails) {
    LoopbackBus bus;
    LoopbackTransport client(bus);

    EXPECT_FALSE(client.connect("127.0.0.1", 9999));

    bool saw_failure = false;
    client.poll_events([&](const TransportEvent& event) {
        if (event.type == TransportEventType::Disconnected && event.reason == DisconnectReason::ConnectionFailed) {
            saw_failure = true;
        }
    });
    EXPECT_TRUE(saw_failure);
}

TEST(NetworkTransport, ClientServerConnectionAndDataExchange) {
    LoopbackBus bus;
    LoopbackTransport server(bus);
    LoopbackTransport client(bus);

    ASSERT_TRUE(server.start_server(7777));
    ASSERT_TRUE(client.connect("127.0.0.1", 7777));

    PeerId connected_client_id = kInvalidPeerId;
    server.poll_events([&](const TransportEvent& event) {
        if (event.type == TransportEventType::Connected) {
            connected_client_id = event.peer_id;
        }
    });

    PeerId connected_server_id = kInvalidPeerId;
    client.poll_events([&](const TransportEvent& event) {
        if (event.type == TransportEventType::Connected) {
            connected_server_id = event.peer_id;
        }
    });

    EXPECT_NE(connected_client_id, kInvalidPeerId);
    EXPECT_EQ(connected_server_id, kServerPeerId);

    // Send from client to server
    const std::vector<std::uint8_t> client_msg = {'H', 'e', 'l', 'l', 'o'};
    EXPECT_TRUE(client.send(kServerPeerId, client_msg, DeliveryMode::ReliableOrdered));

    std::vector<std::uint8_t> server_received;
    PeerId server_received_sender = kInvalidPeerId;
    server.poll_events([&](const TransportEvent& event) {
        if (event.type == TransportEventType::DataReceived) {
            server_received.assign(event.payload.begin(), event.payload.end());
            server_received_sender = event.peer_id;
        }
    });

    EXPECT_EQ(server_received_sender, connected_client_id);
    EXPECT_EQ(server_received, client_msg);

    // Send from server to client
    const std::vector<std::uint8_t> server_msg = {'W', 'o', 'r', 'l', 'd'};
    EXPECT_TRUE(server.send(connected_client_id, server_msg, DeliveryMode::ReliableOrdered));

    std::vector<std::uint8_t> client_received;
    PeerId client_received_sender = kInvalidPeerId;
    client.poll_events([&](const TransportEvent& event) {
        if (event.type == TransportEventType::DataReceived) {
            client_received.assign(event.payload.begin(), event.payload.end());
            client_received_sender = event.peer_id;
        }
    });

    EXPECT_EQ(client_received_sender, kServerPeerId);
    EXPECT_EQ(client_received, server_msg);
}

TEST(NetworkTransport, ClientDisconnects) {
    LoopbackBus bus;
    LoopbackTransport server(bus);
    LoopbackTransport client(bus);

    ASSERT_TRUE(server.start_server(7777));
    ASSERT_TRUE(client.connect("127.0.0.1", 7777));

    PeerId client_id = kInvalidPeerId;
    server.poll_events([&](const TransportEvent& event) {
        if (event.type == TransportEventType::Connected) {
            client_id = event.peer_id;
        }
    });
    client.poll_events([](const TransportEvent&) {});

    ASSERT_NE(client_id, kInvalidPeerId);

    client.disconnect(kServerPeerId);

    bool server_saw_disconnect = false;
    server.poll_events([&](const TransportEvent& event) {
        if (event.type == TransportEventType::Disconnected && event.peer_id == client_id) {
            server_saw_disconnect = true;
            EXPECT_EQ(event.reason, DisconnectReason::ClosedByPeer);
        }
    });
    EXPECT_TRUE(server_saw_disconnect);
}

TEST(NetworkTransport, ServerDisconnectsClient) {
    LoopbackBus bus;
    LoopbackTransport server(bus);
    LoopbackTransport client(bus);

    ASSERT_TRUE(server.start_server(7777));
    ASSERT_TRUE(client.connect("127.0.0.1", 7777));

    PeerId client_id = kInvalidPeerId;
    server.poll_events([&](const TransportEvent& event) {
        if (event.type == TransportEventType::Connected) {
            client_id = event.peer_id;
        }
    });
    client.poll_events([](const TransportEvent&) {});

    ASSERT_NE(client_id, kInvalidPeerId);

    server.disconnect(client_id);

    bool client_saw_disconnect = false;
    client.poll_events([&](const TransportEvent& event) {
        if (event.type == TransportEventType::Disconnected && event.peer_id == kServerPeerId) {
            client_saw_disconnect = true;
            EXPECT_EQ(event.reason, DisconnectReason::ClosedByPeer);
        }
    });
    EXPECT_TRUE(client_saw_disconnect);
}

TEST(NetworkTransport, ServerStopDisconnectsAllClients) {
    LoopbackBus bus;
    LoopbackTransport server(bus);
    LoopbackTransport client(bus);

    ASSERT_TRUE(server.start_server(7777));
    ASSERT_TRUE(client.connect("127.0.0.1", 7777));

    server.poll_events([](const TransportEvent&) {});
    client.poll_events([](const TransportEvent&) {});

    server.stop();

    bool client_saw_disconnect = false;
    client.poll_events([&](const TransportEvent& event) {
        if (event.type == TransportEventType::Disconnected && event.peer_id == kServerPeerId) {
            client_saw_disconnect = true;
        }
    });
    EXPECT_TRUE(client_saw_disconnect);
}

} // namespace engine::net
