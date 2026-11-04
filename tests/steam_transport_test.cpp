#include <gtest/gtest.h>

#include <engine/net/steam_transport.h>

#if defined(ENGINE_WITH_GNS)

#include <engine/net/gns_transport.h>

#include <memory>

namespace engine::net {

TEST(SteamTransport, ServerStartAndStop) {
    SteamTransport server;
    EXPECT_FALSE(server.is_running());
    EXPECT_TRUE(server.start_server(0));
    EXPECT_TRUE(server.is_running());
    EXPECT_TRUE(server.is_server());

    // Starting again while running fails
    EXPECT_FALSE(server.start_server(0));

    server.stop();
    EXPECT_FALSE(server.is_running());
}

TEST(SteamTransport, ConnectInvalidSteamIdFails) {
    SteamTransport client;
    // Malformed address returns false
    EXPECT_FALSE(client.connect("not_a_number", 0));
    EXPECT_FALSE(client.connect("0", 0));
    EXPECT_FALSE(client.connect_steam_id(0, 0));
}

TEST(SteamTransport, CoexistenceWithGnsTransport) {
    auto gns = std::make_unique<GnsTransport>();
    auto steam = std::make_unique<SteamTransport>();

    constexpr std::uint16_t kPort = 27017;
    EXPECT_TRUE(gns->start_server(kPort));
    EXPECT_TRUE(steam->start_server(0));

    // Destroy GNS transport; Steam transport should remain valid and running (ref count not prematurely killed)
    gns.reset();

    EXPECT_TRUE(steam->is_running());
    EXPECT_TRUE(steam->is_server());
    steam->stop();
    EXPECT_FALSE(steam->is_running());
}

} // namespace engine::net

#else

namespace engine::net {

TEST(SteamTransport, UnsupportedWithoutGameNetworkingSockets) {
    SteamTransport server;
    EXPECT_FALSE(server.start_server(0));
    EXPECT_FALSE(server.connect("127.0.0.1", 0));
    EXPECT_FALSE(server.connect_steam_id(76561198000000000ULL, 0));
    EXPECT_FALSE(server.is_running());
    EXPECT_FALSE(server.is_server());
    EXPECT_FLOAT_EQ(server.rtt_ms(1), -1.0f);
}

} // namespace engine::net

#endif
