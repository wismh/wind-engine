#include <gtest/gtest.h>

#include <engine/net/gns_transport.h>

#if defined(ENGINE_WITH_GNS)

#include <engine/core/fixed_step.h>
#include <engine/core/time.h>
#include <engine/ecs/events.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/world.h>
#include <engine/net/byte_stream.h>
#include <engine/net/net_client.h>
#include <engine/net/net_payload_delivered_event.h>
#include <engine/net/net_server.h>

#include <algorithm>
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

// A payload larger than one GameNetworkingSockets message (512 KiB), paced by the real send queue.
TEST(GnsTransport, PayloadAboveTheMessageLimitArrivesWhole) {
    GnsTransport server_transport;
    GnsTransport client_transport;
    constexpr std::uint16_t kPort = 27017;
    constexpr MessageId kChunk = 0x2000;

    ecs::World server;
    ecs::World client;
    NetServerConfig config;
    config.peer_bytes_per_second = 1024 * 1024;
    install_net_server(server, server_transport, config);
    install_net_client(client, client_transport);
    ASSERT_TRUE(server_transport.start_server(kPort));
    ASSERT_TRUE(client_transport.connect("127.0.0.1", kPort));

    std::vector<std::uint8_t> received;
    client.ctx<NetClient>().dispatcher().register_handler(kChunk, [&received](PeerId, ByteReader& reader) {
        const auto bytes = reader.read_bytes(reader.remaining());
        ASSERT_TRUE(bytes.has_value());
        received.assign(bytes->begin(), bytes->end());
    });

    std::vector<std::uint8_t> sent(600 * 1024);
    for (std::size_t i = 0; i < sent.size(); ++i) {
        sent[i] = static_cast<std::uint8_t>(i * 31u + (i >> 8));
    }

    server.ctx<Time>().fixed_delta_time = kFixed;
    PayloadId id = kNoPayload;
    bool delivered = false;
    std::size_t most_pending = 0;
    ecs::EventCursor<NetPayloadDeliveredEvent> cursor{};
    const auto start = std::chrono::steady_clock::now();
    while (!delivered && std::chrono::steady_clock::now() - start < std::chrono::seconds(20)) {
        server.flush_events();
        client.flush_events();
        server.run(ecs::Schedule::Fixed);
        client.run(ecs::Schedule::Frame);
        auto& net = server.ctx<NetServer>();
        if (id == kNoPayload && !net.peers().empty() && client.ctx<NetClient>().welcomed()) {
            id = net.send_payload(net.peers().front(), kChunk, sent);
            ASSERT_NE(id, kNoPayload);
        }
        if (!net.peers().empty()) {
            most_pending = std::max(most_pending, server_transport.pending_send_bytes(net.peers().front()));
        }
        for (const auto& event : ecs::EventReader<NetPayloadDeliveredEvent>(server, cursor)) {
            delivered = delivered || event.id == id;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    EXPECT_TRUE(delivered);
    EXPECT_EQ(received, sent);
    // The backlog limit held, within what one tick of fragments can add on top of it.
    EXPECT_LT(most_pending, config.transport_backlog_bytes + 64u * 1024u);
}

} // namespace engine::net

#else

namespace engine::net {

TEST(GnsTransport, UnsupportedWithoutGns) {
    GnsTransport server;
    EXPECT_FALSE(server.start_server(27015));
    EXPECT_FALSE(server.connect("127.0.0.1", 27015));
    EXPECT_FALSE(server.is_running());
    EXPECT_FALSE(server.is_server());
    EXPECT_FLOAT_EQ(server.rtt_ms(1), -1.0f);
}

} // namespace engine::net

#endif
