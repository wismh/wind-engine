#include <engine/core/server_host.h>

#include "core/game_loop.h"
#include "core/server_fatal_error.h"
#include "core/server_presentation.h"
#include "core/shutdown_signal.h"

#include <engine/core/input_system.h>
#include <engine/core/job_system.h>
#include <engine/core/platform.h>
#include <engine/core/random.h>
#include <engine/core/worlds.h>
#include <engine/ecs/systems.h>
#include <engine/igame.h>
#include <engine/log.h>
#include <engine/net/http_client.h>
#include <engine/process/process_launcher.h>
#include <engine/resources/assets_db.h>

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace engine {

namespace {

constexpr std::string_view kPortKey = "port";

[[nodiscard]] std::string display_path(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

// `--config`, which has to exist, or `server.toml` beside the executable, which may not.
[[nodiscard]] std::expected<Config, EngineError> load_server_config(const ServerOptions& options) {
    const bool is_explicit = !options.config.empty();
    const std::filesystem::path path = is_explicit ? options.config : executable_directory() / "server.toml";
    std::error_code ec;
    if (is_explicit && !std::filesystem::is_regular_file(path, ec)) {
        log::error(std::format("No config file at {}", display_path(path)));
        return std::unexpected(EngineError::ServerConfig);
    }
    auto config = Config::load(path);
    if (!config) {
        log::error(std::format("Config file {}: {}", display_path(path), to_string(config.error())));
        return std::unexpected(EngineError::ServerConfig);
    }
    if (config->has(kPortKey)) {
        const std::int64_t port = config->get_int(kPortKey, 0);
        if (port < 1 || port > 65535) {
            log::error(std::format("Config file {}: `port` is not a whole number in 1..65535", display_path(path)));
            return std::unexpected(EngineError::ServerConfig);
        }
    }
    return *std::move(config);
}

}

std::uint16_t ServerServices::port(std::uint16_t fallback) const {
    if (options.port) {
        return *options.port;
    }
    // load_server_config checked the range.
    return static_cast<std::uint16_t>(config.get_int(kPortKey, fallback));
}

struct ServerHost::Impl {
    explicit Impl(ServerOptions opts)
        : options(std::move(opts)) {}

    ServerOptions options;
    Config config;
    std::filesystem::path assets_root;
    ServerFatalError fatal;
    ServerPresentation presentation;
    GameLoop loop;
    std::unique_ptr<AssetsDb> assets;
    std::unique_ptr<InputSystem> input;
    std::unique_ptr<HttpClient> http;
    std::unique_ptr<ProcessLauncher> processes;
    std::unique_ptr<JobSystem> jobs;
    std::unique_ptr<Worlds> worlds;
    std::optional<ServerServices> services;
    bool initialized = false;
};

ServerHost::ServerHost(ServerOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

ServerHost::~ServerHost() {
    dispose();
}

std::expected<void, EngineError> ServerHost::init() {
    if (impl_->initialized) {
        return {};
    }
    log::init_stdout();

    auto config = load_server_config(impl_->options);
    if (!config) {
        return std::unexpected(config.error());
    }
    impl_->config = *std::move(config);

    // A mistyped --assets would otherwise start a server with nothing to load and fail only mid-game.
    std::error_code assets_error;
    if (!impl_->options.assets.empty() && !std::filesystem::is_directory(impl_->options.assets, assets_error)) {
        log::error(std::format("No assets directory at {}", display_path(impl_->options.assets)));
        return std::unexpected(EngineError::AssetsRoot);
    }
    impl_->assets_root = !impl_->options.assets.empty() ? impl_->options.assets
                                                        : default_assets_root(impl_->presentation.base_path());
    if (impl_->assets_root.empty()) {
        return std::unexpected(EngineError::AssetsRoot);
    }

    impl_->assets = std::make_unique<AssetsDb>(impl_->fatal);
    impl_->input = std::make_unique<InputSystem>();
    impl_->http = std::make_unique<HttpClient>();
    impl_->processes = std::make_unique<ProcessLauncher>();
    impl_->jobs = std::make_unique<JobSystem>();
    impl_->worlds = std::make_unique<Worlds>(impl_->fatal);
    impl_->services.emplace(ServerServices{
            .assets = *impl_->assets,
            .http = *impl_->http,
            .processes = *impl_->processes,
            .jobs = *impl_->jobs,
            .worlds = *impl_->worlds,
            .fatal = impl_->fatal,
            .options = impl_->options,
            .config = impl_->config,
    });
    impl_->initialized = true;

    if (!impl_->http->init()) {
        log::warn("HTTP client failed to start");
    }

    // No graphic factory: CPU assets load, a GPU type answers NotReady. The engine catalog is still read, because a
    // `.world` that names a builtin mesh or material checks the id against it. Either catalog may be absent.
    AssetsDb& assets = *impl_->assets;
    assets.set_root(impl_->assets_root);
    const std::filesystem::path engine_dir = impl_->assets_root / "engine";
    if (const auto loaded = assets.load_catalog(engine_dir / "catalog.toml", engine_dir);
            !loaded && loaded.error() != MetaError::Io) {
        dispose();
        return std::unexpected(EngineError::EngineCatalog);
    }
    if (const auto loaded = assets.load_catalog(impl_->assets_root / "catalog.toml", impl_->assets_root);
            !loaded && loaded.error() != MetaError::Io) {
        dispose();
        return std::unexpected(EngineError::GameCatalog);
    }
    return {};
}

const ServerServices& ServerHost::services() const {
    return *impl_->services;
}

std::filesystem::path ServerHost::assets_root() const {
    return impl_->assets_root;
}

void ServerHost::attach_game(IGame& game) {
    // No commands and no window: set_deps registers simulation only, and run_render stays out. enable_ui and
    // enable_audio are never called.
    impl_->worlds->set_deps(EngineSystemDeps{
            .fatal = &impl_->fatal,
            .worlds = impl_->worlds.get(),
            .jobs = impl_->jobs.get(),
    });
    if (const std::optional<std::uint64_t> seed = game.random_seed(); seed.has_value()) {
        game.world().ctx<Random>().seed(*seed);
    }
}

int ServerHost::run(RunHooks hooks) {
    if (!impl_->initialized) {
        return 1;
    }
    const ShutdownSignals signals;
    const int result = impl_->loop.run(impl_->presentation, std::move(hooks), *impl_->worlds, *impl_->input, nullptr,
            impl_->http.get(), impl_->processes.get(), impl_->jobs.get(), [this] { dispose(); });
    dispose();
    return result;
}

void ServerHost::dispose() {
    if (!impl_->initialized) {
        return;
    }
    impl_->jobs->stop();
    impl_->http->dispose();
    impl_->processes->dispose();
    impl_->initialized = false;
}

void report_server_startup_error(EngineError error) {
    log::error(std::format("{}: {}", to_string(error), message(error)));
}

}
