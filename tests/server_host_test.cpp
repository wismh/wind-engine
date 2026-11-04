#include <gtest/gtest.h>

#include "core/shutdown_signal.h"

#include <engine/core/bound_windows.h>
#include <engine/core/random.h>
#include <engine/core/server.h>
#include <engine/core/time.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world_serializer.h>
#include <engine/log.h>
#include <engine/net/loopback_bus.h>
#include <engine/net/loopback_transport.h>
#include <engine/net/net_client.h>
#include <engine/net/net_server.h>
#include <engine/net/net_entity_map.h>
#include <engine/render/graphic_factory.h>
#include <engine/render/renderable.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/meta.h>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)

namespace engine {
namespace {

// Keeps the test binary's stdout quiet again: ServerHost::init puts the log there.
class ServerHostTest : public ::testing::Test {
protected:
    void TearDown() override {
        log::init();
    }
};

// Counts fixed steps and quits at `limit`. `on_tick` is the test's game logic.
class TickGame final : public GameBase {
public:
    explicit TickGame(const ServerServices& services)
        : GameBase(services.worlds)
        , services_(&services) {}

    void on_start() override {
        calls.push_back("start");
        world().add_system(ecs::Schedule::Fixed, ecs::Phase::Game, [this](ecs::World& w) {
            // quit is read after the iteration, and on a loaded machine one iteration may hold several steps.
            if (limit > 0 && ticks >= limit) {
                return;
            }
            ++ticks;
            if (on_tick) {
                on_tick(w);
            }
            if (limit > 0 && ticks >= limit) {
                worlds().application_state().quit();
            }
        });
        if (after_start) {
            after_start(*this);
        }
    }

    void on_quit() override {
        calls.push_back("quit");
    }

    [[nodiscard]] std::optional<std::uint64_t> random_seed() const override {
        return seed;
    }

    const ServerServices* services_;
    int limit = 0;
    int ticks = 0;
    std::optional<std::uint64_t> seed;
    std::vector<std::string> calls;
    std::function<void(ecs::World&)> on_tick;
    std::function<void(TickGame&)> after_start;
};

[[nodiscard]] ServerOptions empty_assets_options() {
    ServerOptions options;
    // An empty directory: both catalogs are absent, which is fine.
    options.assets = std::filesystem::temp_directory_path() / "wind_server_host_test_no_assets";
    std::filesystem::create_directories(options.assets);
    return options;
}

struct RunningServer {
    ServerHost host;
    std::unique_ptr<TickGame> game;

    explicit RunningServer(ServerOptions options = empty_assets_options())
        : host(std::move(options)) {
        const auto started = host.init();
        EXPECT_TRUE(started.has_value());
        game = std::make_unique<TickGame>(host.services());
        host.attach_game(*game);
    }

    int run(const RunHooks& extra = {}) {
        TickGame& g = *game;
        return host.run(RunHooks{
                .on_start = [&g] { g.on_start(); },
                .on_frame_end = extra.on_frame_end,
                .on_quit = [&g] { g.on_quit(); },
        });
    }
};

TEST_F(ServerHostTest, RunsNTicksWithoutAWindow) {
    RunningServer server;
    server.game->limit = 30;
    EXPECT_EQ(server.run(), 0);
    EXPECT_EQ(server.game->ticks, 30);
    EXPECT_EQ(server.game->calls, (std::vector<std::string>{"start", "quit"}));
    EXPECT_TRUE(server.game->world().ctx<BoundWindows>().ids.empty());
    EXPECT_FALSE(server.host.services().worlds.has_window(server.game->world()));
    EXPECT_TRUE(server.host.services().worlds.presentation().sizes.sizes.empty());
}

TEST_F(ServerHostTest, RegistersNoRenderUiOrAudioSystems) {
    RunningServer server;
    server.game->limit = 2;
    EXPECT_EQ(server.run(), 0);
    ecs::World& world = server.game->world();
    EXPECT_TRUE(world.ctx<SimulationSystemsRegistered>().value);
    EXPECT_FALSE(world.ctx<UiSystemsRegistered>().value);
    EXPECT_FALSE(world.ctx<AudioSystemsRegistered>().value);
    EXPECT_FALSE(world.ctx<EngineSystemsRegistered>().value);
    Worlds& worlds = server.host.services().worlds;
    EXPECT_FALSE(worlds.draws_ui(world));
    // These are what run_render, run_ui_render, and run_audio would need.
    EXPECT_EQ(worlds.deps().commands, nullptr);
    EXPECT_FALSE(static_cast<bool>(worlds.deps().commands_for_window));
    EXPECT_EQ(worlds.deps().audio, nullptr);
    EXPECT_EQ(worlds.deps().assets, nullptr);
}

[[nodiscard]] std::vector<float> run_seeded_world(std::uint64_t seed) {
    RunningServer server;
    server.game->limit = 20;
    server.game->seed = seed;
    std::vector<float> trace;
    server.host.attach_game(*server.game);
    ecs::World& world = server.game->world();
    const ecs::Entity entity = world.create();
    world.emplace<Transform>(entity);
    server.game->on_tick = [&trace, entity](ecs::World& w) {
        Transform& transform = w.get<Transform>(entity);
        transform.position.x += w.ctx<Random>().uniform(-1.0f, 1.0f) * w.ctx<Time>().fixed_delta_time;
        trace.push_back(transform.position.x);
    };
    EXPECT_EQ(server.run(), 0);
    return trace;
}

TEST_F(ServerHostTest, TheSameSeedGivesTheSameWorld) {
    const std::vector<float> first = run_seeded_world(1234);
    const std::vector<float> second = run_seeded_world(1234);
    const std::vector<float> other = run_seeded_world(1235);
    ASSERT_EQ(first.size(), 20u);
    EXPECT_EQ(first, second);
    EXPECT_NE(first, other);
}

TEST_F(ServerHostTest, SleepsToTheTickRate) {
    RunningServer server;
    constexpr int kTicks = 30;
    server.game->limit = kTicks;
    int frames = 0;
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(server.run(RunHooks{.on_start = {}, .on_frame_end = [&frames] { ++frames; }, .on_quit = {}}), 0);
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    // Not faster than real time, within the first step's phase. No upper bound: a loaded machine runs late.
    EXPECT_GE(elapsed, (kTicks - 1) * static_cast<double>(kFixed) * 0.8);
    // About one loop iteration per tick, not a spin. Late wake-ups only lower it.
    EXPECT_LE(frames, kTicks + 3);
}

#if !defined(_WIN32)
TEST(ShutdownSignals, ASecondSignalOfAnotherKindEndsTheProcess) {
    EXPECT_EXIT(
            {
                const ShutdownSignals signals;
                std::raise(SIGINT);
                std::raise(SIGTERM);
                std::_Exit(0);
            },
            ::testing::KilledBySignal(SIGTERM), "");
}
#endif

TEST_F(ServerHostTest, StopsOnASignalWithOnQuit) {
    for (int run = 0; run < 2; ++run) {
        RunningServer server;
        server.game->on_tick = [&server](ecs::World&) {
            if (server.game->ticks == 3) {
                raise_shutdown_signal_for_test();
            }
        };
        EXPECT_EQ(server.run(), 0);
        EXPECT_GE(server.game->ticks, 3);
        // The rest of the iteration that raised it, and the one whose poll sees it.
        EXPECT_LE(server.game->ticks, 3 + 2 * kMaxFixedSteps);
        EXPECT_EQ(server.game->calls, (std::vector<std::string>{"start", "quit"}));
    }
}

TEST_F(ServerHostTest, QuitInOnStartIsIgnoredLikeInEveryHost) {
    RunningServer server;
    server.game->limit = 2;
    server.game->after_start = [](TickGame& game) { game.worlds().application_state().quit(); };
    EXPECT_EQ(server.run(), 0);
    EXPECT_EQ(server.game->ticks, 2);
}

TEST_F(ServerHostTest, HostsANetServerOverLoopback) {
    net::LoopbackBus bus;
    net::LoopbackTransport server_transport{bus};
    net::LoopbackTransport client_transport{bus};

    RunningServer server;
    ecs::World client;
    client.ctx<Time>().fixed_delta_time = kFixed;
    net::install_net_client(client, client_transport);
    bool welcomed = false;
    float server_x = 0.0f;
    std::optional<net::NetId> id;
    server.game->after_start = [&](TickGame& game) {
        ecs::World& world = game.world();
        net::install_net_server(world, server_transport);
        ASSERT_TRUE(server_transport.start_server(1));
        ASSERT_TRUE(client_transport.connect("loopback", 1));
        const ecs::Entity entity = world.create();
        world.emplace<Transform>(entity);
        id = net::replicate(world, entity);
    };
    server.game->on_tick = [&](ecs::World& world) {
        if (const auto entity = net::find_entity(world, *id)) {
            world.get<Transform>(*entity).position.x += 1.0f;
            server_x = world.get<Transform>(*entity).position.x;
        }
        client.flush_events();
        client.ctx<Time>().delta_time = kFixed;
        client.run(ecs::Schedule::Frame);
        welcomed = welcomed || client.ctx<net::NetClient>().welcomed();
        if (welcomed && client.ctx<net::NetClient>().newest_tick() != net::kNoTick && server.game->ticks > 20) {
            server.game->worlds().application_state().quit();
        }
    };
    server.game->limit = 240;
    EXPECT_EQ(server.run(), 0);
    EXPECT_TRUE(welcomed);
    EXPECT_NE(client.ctx<net::NetClient>().newest_tick(), net::kNoTick);
    EXPECT_GT(server_x, 0.0f);
    server_transport.stop();
    client_transport.stop();
}

constexpr const char* kPrototypeGuid = "0123456789abcdef0123456789abcd01";
constexpr const char* kMeshGuid = "0123456789abcdef0123456789abcd02";

class TempAssets {
public:
    TempAssets()
        // Per test: ctest runs them in parallel processes.
        : path(std::filesystem::temp_directory_path() /
                  (std::string("wind_server_host_test_") + ::testing::UnitTest::GetInstance()->current_test_info()->name())) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempAssets() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    void write(const std::string& name, const std::string& text) const {
        std::ofstream out(path / name, std::ios::binary);
        out << text;
    }

    std::filesystem::path path;
};

TEST_F(ServerHostTest, LoadsCpuAssetsWithoutAFactory) {
    TempAssets assets;
    assets.write("thing.prototype", "version = 1\n\n[[entities]]\nid = \"root\"\ntransform = {}\n");
    assets.write("cube.mesh", "");
    CookedCatalog catalog;
    catalog.add({AssetId{kPrototypeGuid}, "thing.prototype", ImporterKind::Prototype});
    catalog.add({AssetId{kMeshGuid}, "cube.mesh", ImporterKind::Mesh});
    assets.write("catalog.toml", catalog.serialize());

    ServerOptions options;
    options.assets = assets.path;
    RunningServer server{std::move(options)};
    AssetsDb& db = server.host.services().assets;
    EXPECT_EQ(server.host.assets_root(), assets.path);

    // A GPU type has no factory to come from; a world that names one still loads, with nothing behind the reference.
    const auto mesh = db.try_get<render::IMesh>(AssetId{kMeshGuid});
    ASSERT_FALSE(mesh.has_value());
    EXPECT_EQ(mesh.error(), AssetError::NotReady);

    ecs::World& world = server.game->world();
    const std::string toml = std::string("version = 1\n\n[[entities]]\nid = \"a\"\ntransform = {}\n"
                                         "renderable = { mesh = \"") +
            kMeshGuid + "\" }\n\n[[entities]]\nid = \"b\"\nprototype = \"asset://" + kPrototypeGuid + "\"\n";
    const auto loaded = ecs::load_world_from_toml(world, toml, &db);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    const render::Renderable* const renderable = world.try_get<render::Renderable>(loaded->by_id.at("a"));
    ASSERT_NE(renderable, nullptr);
    EXPECT_EQ(renderable->mesh, nullptr);
    EXPECT_TRUE(loaded->by_id.contains("b"));
}

TEST_F(ServerHostTest, ACorruptGameCatalogFailsTheStart) {
    TempAssets assets;
    assets.write("catalog.toml", "this is not = = toml");
    ServerOptions options;
    options.assets = assets.path;
    ServerHost host{std::move(options)};
    const auto started = host.init();
    ASSERT_FALSE(started.has_value());
    EXPECT_EQ(started.error(), EngineError::GameCatalog);
}

TEST_F(ServerHostTest, AMissingExplicitAssetsDirectoryFailsTheStart) {
    ServerOptions options;
    options.assets = std::filesystem::temp_directory_path() / "wind_server_host_test_assets_not_created";
    ServerHost host{std::move(options)};
    const auto started = host.init();
    ASSERT_FALSE(started.has_value());
    EXPECT_EQ(started.error(), EngineError::AssetsRoot);
    EXPECT_EQ(host.run({}), 1);
}

TEST_F(ServerHostTest, ConfigFile) {
    TempAssets dir;
    const auto start = [&dir](const std::string& file, std::optional<std::uint16_t> port = std::nullopt) {
        auto options = empty_assets_options();
        options.config = dir.path / file;
        options.port = port;
        return options;
    };

    dir.write("good.toml", "port = 4000\nname = \"x\"\n");
    {
        ServerHost host{start("good.toml")};
        ASSERT_TRUE(host.init().has_value());
        EXPECT_EQ(host.services().port(1), 4000);
        EXPECT_EQ(host.services().config.get_string("name", ""), "x");
    }
    {
        ServerHost host{start("good.toml", 5000)};
        ASSERT_TRUE(host.init().has_value());
        EXPECT_EQ(host.services().port(1), 5000);
    }

    dir.write("noport.toml", "name = \"x\"\n");
    {
        ServerHost host{start("noport.toml")};
        ASSERT_TRUE(host.init().has_value());
        EXPECT_EQ(host.services().port(1234), 1234);
    }

    dir.write("invalid.toml", "= = =");
    dir.write("range.toml", "port = 70000\n");
    dir.write("text.toml", "port = \"x\"\n");
    for (const char* const bad : {"invalid.toml", "range.toml", "text.toml", "missing.toml"}) {
        ServerHost host{start(bad)};
        const auto started = host.init();
        ASSERT_FALSE(started.has_value()) << bad;
        EXPECT_EQ(started.error(), EngineError::ServerConfig) << bad;
    }

    // No --config: server.toml beside the executable may be absent.
    ServerHost host{empty_assets_options()};
    ASSERT_TRUE(host.init().has_value());
    EXPECT_EQ(host.services().port(77), 77);
}

TEST_F(ServerHostTest, RunBeforeInitReturnsOne) {
    ServerHost host{empty_assets_options()};
    EXPECT_EQ(host.run({}), 1);
}

}
}

#endif
