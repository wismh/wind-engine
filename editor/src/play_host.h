#pragma once

#include <expected>
#include <filesystem>
#include <string>

namespace engine {
class IGame;
}

namespace editor {

// The window and catalog half of Play and Stop that needs the real presentation. The editor implements
// it over EngineHost (EngineHostPlay); the play-session test implements it with a recorder.
class IPlayHost {
public:
    virtual ~IPlayHost() = default;

    // `<assets_dir>/catalog.toml`. A missing file is not an error.
    [[nodiscard]] virtual std::expected<void, std::string> load_catalog(const std::filesystem::path& assets_dir) = 0;
    virtual void unload_catalog(const std::filesystem::path& assets_dir) = 0;
    // Binds kPrimaryWindow to the game's world: EngineHost::attach_game.
    virtual void attach(engine::IGame& game) = 0;
    // Unbinds kPrimaryWindow and clears what it holds of the game: EngineHost::detach_game.
    virtual void detach() = 0;
};

}
