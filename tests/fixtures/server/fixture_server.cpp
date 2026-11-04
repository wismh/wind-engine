// A small dedicated server for tests/server_process_test.cpp, built by engine_add_server the way a game's is. It uses
// public headers only. Its flags go through ServerOptions::args:
//   --transport                 listen with GnsTransport on the port (27100 unless told otherwise)
//   --signal-self-after <n>     deliver a real stop signal to this process after n ticks
//   --stall-after <n>           block one tick for half a second after n ticks, so ticks get dropped
//   --fatal-on-start            report a fatal error from on_start

#include <engine/core/time.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/igame.h>
#include <engine/log.h>
#include <engine/net/gns_transport.h>
#include <engine/net/byte_stream.h>
#include <engine/net/message_dispatcher.h>
#include <engine/net/net_entity_map.h>
#include <engine/net/net_server.h>
#include <engine/resources/fatal_error.h>
#include <engine/server_entry.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#endif

namespace fixture {

// The game's own message a test client sends to stop this server.
constexpr engine::net::MessageId kQuitMessage = 0x2001;

// Sent by the operating system in real use. The process has its own console (no window) under ProcessLauncher, so on
// Windows the event reaches only this process.
void signal_this_process() {
#if defined(_WIN32)
    GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0);
#else
    std::raise(SIGTERM);
#endif
}

[[nodiscard]] std::optional<int> int_after(const engine::ServerOptions& options, std::string_view flag) {
    for (std::size_t i = 0; i + 1 < options.args.size(); ++i) {
        if (options.args[i] == flag) {
            const std::string& text = options.args[i + 1];
            int value = 0;
            std::from_chars(text.data(), text.data() + text.size(), value);
            return value;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool has_flag(const engine::ServerOptions& options, std::string_view flag) {
    for (const std::string& argument : options.args) {
        if (argument == flag) {
            return true;
        }
    }
    return false;
}

class Server final : public engine::GameBase {
public:
    explicit Server(const engine::ServerServices& services)
        : engine::GameBase(services.worlds)
        , fatal_(&services.fatal)
        , port_(services.port(27100))
        , listen_(has_flag(services.options, "--transport"))
        , fatal_on_start_(has_flag(services.options, "--fatal-on-start"))
        , signal_after_(int_after(services.options, "--signal-self-after"))
        , stall_after_(int_after(services.options, "--stall-after")) {}

    void on_start() override {
        if (fatal_on_start_) {
            fatal_->report("fixture: fatal on start");
        }
        engine::ecs::World& game_world = world();
        if (listen_) {
            engine::net::install_net_server(game_world, transport_);
            if (!transport_.start_server(port_)) {
                fatal_->report(std::format("fixture: could not listen on port {}", port_));
            }
            game_world.ctx<engine::net::NetServer>().dispatcher().register_handler(
                    kQuitMessage, [this](engine::net::PeerId, engine::net::ByteReader&) {
                        engine::log::info("fixture: quit message");
                        worlds().application_state().quit();
                    });
            const engine::ecs::Entity entity = game_world.create();
            game_world.emplace<engine::Transform>(entity);
            moving_ = engine::net::replicate(game_world, entity);
        }
        game_world.add_system(engine::ecs::Schedule::Fixed, engine::ecs::Phase::Game,
                [this](engine::ecs::World& w) { tick(w); });
        engine::log::info(std::format("fixture: started on port {}", port_));
    }

    void on_quit() override {
        engine::log::info(std::format("fixture: on_quit after {} ticks", ticks_));
        transport_.stop();
        engine::log::info("fixture: transport stopped");
    }

private:
    void tick(engine::ecs::World& w) {
        ++ticks_;
        if (moving_) {
            if (const auto entity = engine::net::find_entity(w, *moving_)) {
                w.get<engine::Transform>(*entity).position.x += engine::kFixed;
            }
        }
        if (signal_after_ && ticks_ == *signal_after_) {
            signal_this_process();
        }
        if (stall_after_ && ticks_ == *stall_after_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    engine::IFatalError* fatal_;
    std::uint16_t port_;
    bool listen_;
    bool fatal_on_start_;
    std::optional<int> signal_after_;
    std::optional<int> stall_after_;
    engine::net::GnsTransport transport_;
    std::optional<engine::net::NetId> moving_;
    int ticks_ = 0;
};

}

ENGINE_GAME_SERVER(fixture::Server)
