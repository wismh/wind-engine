#include <gtest/gtest.h>

#include <engine/net/steam_transport.h>

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

} // namespace engine::net
