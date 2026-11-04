#include <gtest/gtest.h>

// A real server process: tests/fixtures/server, built by engine_add_server, started through ProcessLauncher.

#if defined(WIND_SERVER_FIXTURE)

#include <engine/core/time.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/net/message_dispatcher.h>
#include <engine/net/gns_transport.h>
#include <engine/net/net_client.h>
#include <engine/net/net_entity_map.h>
#include <engine/process/process_launcher.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace engine {
namespace {

constexpr auto kProcessTimeout = std::chrono::seconds(30);

struct Finished {
    std::optional<int> code;
    std::vector<std::string> output;
};

class ServerProcess {
public:
    explicit ServerProcess(std::vector<std::string> arguments) {
        call_ = launcher_.run(ProcessDesc{
                .program = WIND_SERVER_FIXTURE,
                .arguments = std::move(arguments),
                .working_directory = {},
                .environment = {},
        });
    }

    ~ServerProcess() {
        call_.cancel();
        launcher_.dispose();
    }

    void poll() {
        launcher_.poll();
        for (std::string& line : call_.take_output()) {
            output_.push_back(std::move(line));
        }
        if (!result_) {
            result_ = call_.take();
        }
    }

    [[nodiscard]] bool said(std::string_view text) const {
        return index_of(text).has_value();
    }

    [[nodiscard]] std::optional<std::size_t> index_of(std::string_view text) const {
        for (std::size_t i = 0; i < output_.size(); ++i) {
            if (output_[i].find(text) != std::string::npos) {
                return i;
            }
        }
        return std::nullopt;
    }

    // The exit code, nullopt when it did not end in time.
    [[nodiscard]] std::optional<int> wait() {
        const auto start = std::chrono::steady_clock::now();
        while (!result_ && std::chrono::steady_clock::now() - start < kProcessTimeout) {
            poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!result_ || !result_->has_value()) {
            return std::nullopt;
        }
        // Lines that raced the exit.
        poll();
        return (*result_)->code;
    }

    [[nodiscard]] std::string all_output() const {
        std::string text;
        for (const std::string& line : output_) {
            text += line;
            text += '\n';
        }
        return text;
    }

private:
    ProcessLauncher launcher_;
    ProcessCall call_;
    std::vector<std::string> output_;
    std::optional<ProcessResult> result_;
};

TEST(ServerProcessTest, HelpPrintsTheUsageAndExitsZero) {
    ServerProcess server({"--help"});
    EXPECT_EQ(server.wait(), 0);
    EXPECT_TRUE(server.said("--port")) << server.all_output();
    EXPECT_FALSE(server.said("started")) << server.all_output();
}

TEST(ServerProcessTest, ABadCommandLineExitsTwo) {
    ServerProcess server({"--port", "not-a-port"});
    EXPECT_EQ(server.wait(), 2);
    EXPECT_TRUE(server.said("not-a-port")) << server.all_output();
}

TEST(ServerProcessTest, AMissingConfigFileExitsOne) {
    ServerProcess server({"--config", "wind-no-such-config.toml"});
    EXPECT_EQ(server.wait(), 1);
    EXPECT_TRUE(server.said("ServerConfig")) << server.all_output();
}

TEST(ServerProcessTest, StopsCleanlyOnARealSignalAndLogsToStdout) {
    ServerProcess server({"--signal-self-after", "30"});
    EXPECT_EQ(server.wait(), 0) << server.all_output();
    const auto started = server.index_of("fixture: started");
    const auto stop = server.index_of("Stop signal received");
    const auto quit = server.index_of("fixture: on_quit");
    const auto stopped = server.index_of("fixture: transport stopped");
    ASSERT_TRUE(started && stop && quit && stopped) << server.all_output();
    EXPECT_LT(*started, *stop);
    EXPECT_LT(*stop, *quit);
    EXPECT_LT(*quit, *stopped);
}

TEST(ServerProcessTest, AFatalOnStartLogsAndExitsOneWithoutOnQuit) {
    ServerProcess server({"--fatal-on-start"});
    EXPECT_EQ(server.wait(), 1);
    EXPECT_TRUE(server.said("fixture: fatal on start")) << server.all_output();
    EXPECT_FALSE(server.said("fixture: on_quit")) << server.all_output();
}

TEST(ServerProcessTest, ALongStallWarnsAboutDroppedTicksOnce) {
    ServerProcess server({"--stall-after", "5", "--signal-self-after", "40"});
    EXPECT_EQ(server.wait(), 0) << server.all_output();
    EXPECT_TRUE(server.said("dropped ticks")) << server.all_output();
}

#if defined(ENGINE_WITH_GNS)

constexpr net::MessageId kQuitMessage = 0x2001;

TEST(ServerProcessTest, ClientOverARealSocketSeesTheWorldAndTheClose) {
    constexpr std::uint16_t kPort = 27118;
    ServerProcess server({"--port", std::to_string(kPort), "--transport"});

    // The handshake repeats until the server has bound.
    net::GnsTransport transport;
    ecs::World client;
    client.ctx<Time>().fixed_delta_time = kFixed;
    net::install_net_client(client, transport);
    ASSERT_TRUE(transport.connect("127.0.0.1", kPort));

    bool welcomed = false;
    std::optional<float> first_x;
    std::optional<float> later_x;
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(20) && !later_x) {
        server.poll();
        client.flush_events();
        client.ctx<Time>().delta_time = kFixed;
        client.run(ecs::Schedule::Frame);
        welcomed = welcomed || client.ctx<net::NetClient>().welcomed();
        client.view<Transform>().each([&](ecs::Entity, const Transform& transform) {
            if (!first_x) {
                first_x = transform.position.x;
            } else if (transform.position.x > *first_x) {
                later_x = transform.position.x;
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    ASSERT_TRUE(welcomed) << server.all_output();
    ASSERT_TRUE(later_x.has_value()) << "no replicated motion\n" << server.all_output();

    // The game's own message stops the server; the client hears the close at once, not after a timeout.
    ASSERT_TRUE(net::MessageDispatcher::send(transport, net::kServerPeerId, kQuitMessage, {}, net::DeliveryMode::ReliableOrdered));

    bool closed = false;
    const auto stop = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - stop < std::chrono::seconds(3) && !closed) {
        server.poll();
        client.flush_events();
        client.run(ecs::Schedule::Frame);
        closed = !client.ctx<net::NetClient>().welcomed();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(closed) << "the client did not see the server close\n" << server.all_output();
    EXPECT_EQ(server.wait(), 0) << server.all_output();
    EXPECT_TRUE(server.said("fixture: quit message")) << server.all_output();
    EXPECT_TRUE(server.said("fixture: transport stopped")) << server.all_output();
    transport.stop();
}

#endif

}
}

#endif
