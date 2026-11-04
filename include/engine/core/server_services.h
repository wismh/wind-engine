#pragma once

// docs/tech/modules/Core.md

#include <engine/core/config.h>
#include <engine/core/job_system.h>
#include <engine/core/server_options.h>
#include <engine/core/worlds.h>

#include <cstdint>

namespace engine {

class AssetsDb;
class IFatalError;
class IHttpClient;
class IProcessLauncher;

// References handed to a dedicated server at construction. `ServerHost` owns every object. There is no window,
// graphics, audio, or input here: a server that reached for them would not compile. Assets load without a graphics
// factory: CPU types work, and a GPU type answers `AssetError::NotReady`.
struct ServerServices {
    AssetsDb& assets;
    IHttpClient& http;
    IProcessLauncher& processes;
    JobSystem& jobs;
    Worlds& worlds;
    // Logs, then exits with code 1. How `on_start` says the server cannot run (`quit` there is ignored, as in every
    // host, and `on_quit` does not run after this).
    IFatalError& fatal;
    const ServerOptions& options;
    // The config file (`--config`, or `<executable dir>/server.toml`), empty when there is none.
    const Config& config;

    // --port, else `port` in the config file, else `fallback`.
    [[nodiscard]] std::uint16_t port(std::uint16_t fallback) const;
};

}
