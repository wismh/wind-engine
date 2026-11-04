#pragma once

// docs/tech/modules/Core.md

#include <engine/core/engine_error.h>
#include <engine/core/server_host.h>
#include <engine/core/server_options.h>
#include <engine/core/server_services.h>
#include <engine/igame.h>

#include <concepts>
#include <expected>
#include <memory>
#include <utility>

namespace engine {

template<typename GameT>
concept ServerGame = std::derived_from<GameT, IGame> && std::constructible_from<GameT, const ServerServices&>;

// One `ServerHost` and one `GameT`. Games normally get this from `ENGINE_GAME_SERVER(GameClass)`.
template<ServerGame GameT>
class Server {
public:
    explicit Server(ServerOptions options)
        : host_(std::move(options)) {}

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Builds the host and the game. A failure disposes everything and returns the cause.
    [[nodiscard]] std::expected<void, EngineError> init() {
        if (game_) {
            return {};
        }
        if (const auto started = host_.init(); !started) {
            return started;
        }
        game_ = std::make_unique<GameT>(host_.services());
        host_.attach_game(*game_);
        return {};
    }

    // Runs until `ApplicationState::quit`, a signal, or a fatal error. Returns 1 when `init` has not succeeded.
    int run() {
        if (!game_) {
            return 1;
        }
        GameT& game = *game_;
        return host_.run(RunHooks{
                .on_start = [&game] { game.on_start(); },
                .on_frame_end = {},
                .on_quit = [&game] { game.on_quit(); },
        });
    }

private:
    // Declared after host_, so the game is destroyed first while the services it references live.
    ServerHost host_;
    std::unique_ptr<GameT> game_;
};

// The body of `ENGINE_GAME_SERVER`. Exit code 0 after a clean stop or --help, 2 for a bad command line, 1 when the
// server could not start.
template<ServerGame GameT>
int run_server(int argc, char** argv) {
    auto options = parse_server_args(process_arguments(argc, argv));
    if (!options) {
        report_server_arg_problem(options.error());
        return 2;
    }
    if (options->help) {
        print_server_usage();
        return 0;
    }
    Server<GameT> server{*std::move(options)};
    if (const auto started = server.init(); !started) {
        report_server_startup_error(started.error());
        return 1;
    }
    return server.run();
}

}
