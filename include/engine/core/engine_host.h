#pragma once

// docs/tech/modules/Core.md

#if !defined(ENGINE_WITH_WINDOW)
#error "engine::EngineHost requires ENGINE_WITH_WINDOW"
#endif

#include <engine/core/engine_services.h>
#include <engine/core/run_hooks.h>
#include <engine/core/window_desc.h>
#include <engine/resources/meta.h>

#include <expected>
#include <filesystem>
#include <memory>

namespace engine {

class IFatalError;
class IGame;

// Owns every engine service of a windowed process: runtime, fatal hook, assets, input, audio, haptics,
// and worlds. `Engine<GameT>` sits on it, and so does the editor host. Call in order: `init`, `open_primary`,
// `load_game_catalog`, `attach_game`, `run`. `dispose` (also run by the destructor and at the end of
// `run`) disposes audio and haptics and shuts the runtime down.
class EngineHost {
public:
    EngineHost();
    ~EngineHost();

    EngineHost(const EngineHost&) = delete;
    EngineHost& operator=(const EngineHost&) = delete;

    // SDL video, the log file, and every service. `services()` is valid once this returned true.
    [[nodiscard]] bool init();
    [[nodiscard]] const EngineServices& services() const;
    [[nodiscard]] IFatalError& fatal();

    // Creates `kPrimaryWindow`, starts audio and haptics, loads the engine catalog and
    // `builtin::font_ui`, and hands the system deps to `Worlds`. A failure shuts the host down.
    [[nodiscard]] bool open_primary(const WindowDesc& desc);

    // `<executable dir>/assets` (`/assets` on web, staged storage on Android).
    [[nodiscard]] std::filesystem::path assets_root() const;

    // Loads `<assets_dir>/catalog.toml` with `assets_dir` as its files root. A missing file is not an
    // error: a game without assets has no catalog.
    [[nodiscard]] std::expected<void, MetaError> load_game_catalog(const std::filesystem::path& assets_dir);
    void unload_game_catalog(const std::filesystem::path& assets_dir);

    // Window icon, binds `kPrimaryWindow` to the game world, enables UI and audio on it, publishes the
    // window size, and fits its canvases.
    void attach_game(IGame& game);

    // Runs the loop until `ApplicationState::running` is false, then disposes. Returns 1 when
    // `open_primary` has not succeeded.
    [[nodiscard]] int run(RunHooks hooks);
    void dispose();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
