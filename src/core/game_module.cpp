#include <engine/core/game_module.h>

#include <engine/core/build_info.h>
#include <engine/log.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_loadso.h>

#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace engine {
namespace {

std::string path_text(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

// The first `<live_root>/<n>` that did not exist yet. Empty when none could be created.
std::filesystem::path make_live_dir(const std::filesystem::path& live_root, std::error_code& ec) {
    std::filesystem::create_directories(live_root, ec);
    if (ec) {
        return {};
    }
    constexpr int kMaxAttempts = 10000;
    for (int n = 1; n <= kMaxAttempts; ++n) {
        std::filesystem::path dir = live_root / std::to_string(n);
        if (std::filesystem::create_directory(dir, ec)) {
            return dir;
        }
        if (ec) {
            return {};
        }
    }
    ec = std::make_error_code(std::errc::file_exists);
    return {};
}

void remove_dir(const std::filesystem::path& dir) {
    if (dir.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    if (ec) {
        log::warn("Could not delete game module copy " + path_text(dir) + ": " + ec.message());
    }
}

}

std::string_view to_string(ModuleError error) noexcept {
    switch (error) {
        case ModuleError::Missing:
            return "Game module not found";
        case ModuleError::CopyFailed:
            return "Could not copy the game module";
        case ModuleError::LoadFailed:
            return "Could not load the game module";
        case ModuleError::MissingSymbol:
            return "Not a Wind game module (missing export)";
        case ModuleError::BuildIdMismatch:
            return "Game built against another engine build";
    }
    return "Game module error";
}

std::string describe(const GameModuleError& error) {
    std::string text(to_string(error.kind));
    if (!error.detail.empty()) {
        text += ": ";
        text += error.detail;
    }
    return text;
}

GameModule::GameModule(GameModule&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr))
    , create_(std::exchange(other.create_, nullptr))
    , destroy_(std::exchange(other.destroy_, nullptr))
    , build_id_(std::move(other.build_id_))
    , source_path_(std::move(other.source_path_))
    , live_dir_(std::exchange(other.live_dir_, {}))
    , live_path_(std::move(other.live_path_)) {}

GameModule& GameModule::operator=(GameModule&& other) noexcept {
    if (this != &other) {
        release();
        handle_ = std::exchange(other.handle_, nullptr);
        create_ = std::exchange(other.create_, nullptr);
        destroy_ = std::exchange(other.destroy_, nullptr);
        build_id_ = std::move(other.build_id_);
        source_path_ = std::move(other.source_path_);
        live_dir_ = std::exchange(other.live_dir_, {});
        live_path_ = std::move(other.live_path_);
    }
    return *this;
}

GameModule::~GameModule() {
    release();
}

IGame* GameModule::create(const EngineServices& services) const {
    return create_(services);
}

void GameModule::destroy(IGame* game) const {
    destroy_(game);
}

std::string_view GameModule::build_id() const noexcept {
    return build_id_;
}

const std::filesystem::path& GameModule::source_path() const noexcept {
    return source_path_;
}

const std::filesystem::path& GameModule::live_path() const noexcept {
    return live_path_;
}

void GameModule::release() noexcept {
    if (handle_ != nullptr) {
        SDL_UnloadObject(static_cast<SDL_SharedObject*>(handle_));
        handle_ = nullptr;
    }
    create_ = nullptr;
    destroy_ = nullptr;
    remove_dir(live_dir_);
    live_dir_.clear();
}

std::expected<GameModule, GameModuleError> load_game_module(
        const std::filesystem::path& module, const std::filesystem::path& live_root) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(module, ec)) {
        return std::unexpected(GameModuleError{ModuleError::Missing, path_text(module)});
    }

    GameModule loaded;
    loaded.source_path_ = module;
    loaded.live_dir_ = make_live_dir(live_root, ec);
    if (loaded.live_dir_.empty()) {
        return std::unexpected(GameModuleError{ModuleError::CopyFailed, path_text(live_root) + ": " + ec.message()});
    }
    loaded.live_path_ = loaded.live_dir_ / module.filename();
    if (!std::filesystem::copy_file(module, loaded.live_path_, ec)) {
        return std::unexpected(GameModuleError{ModuleError::CopyFailed, path_text(module) + ": " + ec.message()});
    }
    // The module records only the PDB file name (/PDBALTPATH), so a debugger looks beside the copy.
    std::filesystem::path pdb = module;
    pdb.replace_extension(".pdb");
    if (std::filesystem::is_regular_file(pdb, ec)) {
        if (!std::filesystem::copy_file(pdb, loaded.live_dir_ / pdb.filename(), ec)) {
            log::warn("Could not copy " + path_text(pdb) + ": " + ec.message() + ". Debug symbols are missing");
        }
    }

    const std::string live_text = path_text(loaded.live_path_);
    loaded.handle_ = SDL_LoadObject(live_text.c_str());
    if (loaded.handle_ == nullptr) {
        return std::unexpected(GameModuleError{ModuleError::LoadFailed, SDL_GetError()});
    }
    auto* const object = static_cast<SDL_SharedObject*>(loaded.handle_);
    loaded.create_ = reinterpret_cast<CreateGameFn>(SDL_LoadFunction(object, kCreateGameSymbol));
    loaded.destroy_ = reinterpret_cast<DestroyGameFn>(SDL_LoadFunction(object, kDestroyGameSymbol));
    const auto game_build_id = reinterpret_cast<GameBuildIdFn>(SDL_LoadFunction(object, kGameBuildIdSymbol));
    if (loaded.create_ == nullptr) {
        return std::unexpected(GameModuleError{ModuleError::MissingSymbol, kCreateGameSymbol});
    }
    if (loaded.destroy_ == nullptr) {
        return std::unexpected(GameModuleError{ModuleError::MissingSymbol, kDestroyGameSymbol});
    }
    if (game_build_id == nullptr) {
        return std::unexpected(GameModuleError{ModuleError::MissingSymbol, kGameBuildIdSymbol});
    }

    const char* const id = game_build_id();
    loaded.build_id_ = id != nullptr ? id : "";
    if (loaded.build_id_ != build_id()) {
        return std::unexpected(GameModuleError{ModuleError::BuildIdMismatch,
                "module " + loaded.build_id_ + ", editor " + std::string(build_id())});
    }
    return loaded;
}

std::size_t purge_game_module_copies(const std::filesystem::path& live_root) {
    std::error_code ec;
    std::filesystem::directory_iterator it(live_root, ec);
    if (ec) {
        return 0;
    }
    std::vector<std::filesystem::path> dirs;
    for (const std::filesystem::directory_entry& entry : it) {
        if (entry.is_directory(ec)) {
            dirs.push_back(entry.path());
        }
    }
    std::size_t removed = 0;
    for (const std::filesystem::path& dir : dirs) {
        std::filesystem::remove_all(dir, ec);
        if (ec) {
            log::warn("Could not delete stale game module copy " + path_text(dir) + ": " + ec.message());
        } else {
            ++removed;
        }
    }
    return removed;
}

}
