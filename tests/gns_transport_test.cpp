#include <gtest/gtest.h>

#include <engine/net/gns_transport.h>

#if defined(_WIN32)

#include <chrono>
#include <thread>
#include <vector>

namespace engine::net {

TEST(GnsTransport, ServerStartAndStop) {
    GnsTransport server;
    EXPECT_FALSE(server.is_running());
    EXPECT_TRUE(server.start_server(27015));
    EXPECT_TRUE(server.is_running());
    EXPECT_TRUE(server.is_server());

    // Starting again while running should fail
    EXPECT_FALSE(server.start_server(27015));

    server.stop();
    EXPECT_FALSE(server.is_running());
}

TEST(GnsTransport, ClientServerRealUdpExchange) {
    GnsTransport server;
    GnsTransport client;

    constexpr std::uint16_t kPort = 27016;
    ASSERT_TRUE(server.start_server(kPort));
    ASSERT_TRUE(client.connect("127.0.0.1", kPort));

    PeerId server_connected_client = kInvalidPeerId;
    bool client_connected = false;

    // Wait up to 2 seconds for UDP connection handshake
    const auto start_time = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(2)) {
        server.poll_events([&](const TransportEvent& ev) {
            if (ev.type == TransportEventType::Connected) {
                server_connected_client = ev.peer_id;
            }
        });
        client.poll_events([&](const TransportEvent& ev) {
            if (ev.type == TransportEventType::Connected) {
                client_connected = true;
            }
        });
        if (server_connected_client != kInvalidPeerId && client_connected) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_TRUE(client_connected);
    ASSERT_NE(server_connected_client, kInvalidPeerId);

    // Send payload from client to server over UDP
    const std::vector<std::uint8_t> client_payload = {0xDE, 0xAD, 0xBE, 0xEF};
    EXPECT_TRUE(client.send(kServerPeerId, client_payload, DeliveryMode::ReliableOrdered));

    std::vector<std::uint8_t> server_received;
    const auto send_time = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - send_time < std::chrono::seconds(1)) {
        server.poll_events([&](const TransportEvent& ev) {
            if (ev.type == TransportEventType::DataReceived) {
                server_received.assign(ev.payload.begin(), ev.payload.end());
            }
        });
        client.poll_events([](const TransportEvent&) {});
        if (!server_received.empty()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_EQ(server_received, client_payload);

    // Disconnect client
    client.disconnect(kServerPeerId);
    bool server_saw_disconnect = false;
    const auto dc_time = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - dc_time < std::chrono::seconds(1)) {
        server.poll_events([&](const TransportEvent& ev) {
            if (ev.type == TransportEventType::Disconnected) {
                server_saw_disconnect = true;
            }
        });
        client.poll_events([](const TransportEvent&) {});
        if (server_saw_disconnect) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(server_saw_disconnect);
}

} // namespace engine::net

#else

namespace engine::net {

TEST(GnsTransport, UnsupportedOnNonWindows) {
    GnsTransport server;
    EXPECT_FALSE(server.start_server(27015));
    EXPECT_FALSE(server.connect("127.0.0.1", 27015));
    EXPECT_FALSE(server.is_running());
    EXPECT_FALSE(server.is_server());
    EXPECT_FLOAT_EQ(server.rtt_ms(1), -1.0f);
}

} // namespace engine::net

#endif
