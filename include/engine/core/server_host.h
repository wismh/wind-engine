#pragma once

// docs/tech/modules/Core.md

#include <engine/core/engine_error.h>
#include <engine/core/run_hooks.h>
#include <engine/core/server_options.h>
#include <engine/core/server_services.h>

#include <expected>
#include <filesystem>
#include <memory>

namespace engine {

class IGame;

// Owns every engine service of a dedicated server: fatal hook, assets, HTTP, processes, jobs, and worlds. No SDL, no
// window, and no render, UI, or audio system: it compiles and runs with ENGINE_WITH_WINDOW off. `Server<GameT>` sits
// on it. Call in order: `init`, `attach_game`, `run`.
class ServerHost {
public:
    explicit ServerHost(ServerOptions options);
    ~ServerHost();

    ServerHost(const ServerHost&) = delete;
    ServerHost& operator=(const ServerHost&) = delete;

    // The log on stdout, the config file, every service, and the engine and game catalogs (either may be absent).
    // Fails with `ServerConfig`, `AssetsRoot`, `EngineCatalog`, or `GameCatalog`, having disposed the host.
    [[nodiscard]] std::expected<void, EngineError> init();
    [[nodiscard]] const ServerServices& services() const;

    [[nodiscard]] std::filesystem::path assets_root() const;

    // Registers the simulation systems on the game's worlds and applies `IGame::random_seed`. Nothing else: no window
    // is bound, and UI and audio stay off.
    void attach_game(IGame& game);

    // Installs the signal handlers and runs the loop until `ApplicationState::running` is false, then disposes.
    // SIGINT, SIGTERM, SIGHUP, Ctrl+C, Ctrl+Break, a closing console, a logoff, or a system shutdown stop it after the
    // tick in progress, with `RunHooks::on_quit`. A second signal, of any kind, has its default effect, so a hung shutdown can still
    // be interrupted. Returns 0, or 1 when `init` has not succeeded.
    [[nodiscard]] int run(RunHooks hooks);
    void dispose();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Logs why the server could not start. The entry point returns exit code 1 after it.
void report_server_startup_error(EngineError error);

}
