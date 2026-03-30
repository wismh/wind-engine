#pragma once

// docs/tech/modules/Core.md

#include <engine/core/engine_services.h>
#include <engine/igame.h>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

// The C ABI of a game module (`ENGINE_GAME` under `ENGINE_GAME_MODULE`). The editor resolves these
// three symbols by name. wind_game_build_id returns the kBuildId the game was compiled against, not
// engine::build_id(): inside the editor that call would reach the editor's own engine.

#if defined(_WIN32)
#define ENGINE_GAME_EXPORT extern "C" __declspec(dllexport)
#else
#define ENGINE_GAME_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace engine {

using CreateGameFn = IGame* (*)(const EngineServices& services);
using DestroyGameFn = void (*)(IGame* game);
using GameBuildIdFn = const char* (*)();

inline constexpr char kCreateGameSymbol[] = "wind_create_game";
inline constexpr char kDestroyGameSymbol[] = "wind_destroy_game";
inline constexpr char kGameBuildIdSymbol[] = "wind_game_build_id";

#if defined(ENGINE_WITH_WINDOW)

enum class ModuleError {
    Missing,
    CopyFailed,
    LoadFailed,
    MissingSymbol,
    BuildIdMismatch,
};

struct GameModuleError {
    ModuleError kind = ModuleError::Missing;
    // The path, the platform loader message, the symbol name, or both build ids.
    std::string detail;
};

[[nodiscard]] std::string_view to_string(ModuleError error) noexcept;
// One line for a status bar: "<what failed>: <detail>".
[[nodiscard]] std::string describe(const GameModuleError& error);

// A loaded copy of a game module. Unloading frees the module's code, so every object the game built
// (its `IGame`, worlds, systems, view-models) must be gone before this is destroyed or reassigned.
// The destructor unloads the copy and deletes its directory.
class GameModule {
public:
    GameModule(GameModule&& other) noexcept;
    GameModule& operator=(GameModule&& other) noexcept;
    GameModule(const GameModule&) = delete;
    GameModule& operator=(const GameModule&) = delete;
    ~GameModule();

    // `wind_create_game`. The result belongs to this module: pass it to `destroy`, not `delete`.
    [[nodiscard]] IGame* create(const EngineServices& services) const;
    // `wind_destroy_game`.
    void destroy(IGame* game) const;

    // What the module's `wind_game_build_id` returned. Equal to `build_id()` for a loaded module.
    [[nodiscard]] std::string_view build_id() const noexcept;
    // The module the caller asked for.
    [[nodiscard]] const std::filesystem::path& source_path() const noexcept;
    // The copy that is actually loaded, `<live root>/<n>/<file name>`.
    [[nodiscard]] const std::filesystem::path& live_path() const noexcept;

private:
    friend std::expected<GameModule, GameModuleError> load_game_module(
            const std::filesystem::path& module, const std::filesystem::path& live_root);

    GameModule() = default;
    void release() noexcept;

    void* handle_ = nullptr;
    CreateGameFn create_ = nullptr;
    DestroyGameFn destroy_ = nullptr;
    std::string build_id_;
    std::filesystem::path source_path_;
    std::filesystem::path live_dir_;
    std::filesystem::path live_path_;
};

// Copies `module` and the `.pdb` beside it, when there is one, into a new directory `<live_root>/<n>/`
// and loads that copy, so the original stays free for the next build. Then resolves the three exports
// and compares `wind_game_build_id` with `build_id()`. Any failure unloads and deletes the copy.
// Main thread only.
[[nodiscard]] std::expected<GameModule, GameModuleError> load_game_module(
        const std::filesystem::path& module, const std::filesystem::path& live_root);

// Deletes every directory under `live_root`: copies a crashed or killed process left behind. A copy
// another process still has loaded cannot be deleted and stays. Returns how many directories went away.
std::size_t purge_game_module_copies(const std::filesystem::path& live_root);

#endif

}
