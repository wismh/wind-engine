#pragma once

#include <expected>
#include <filesystem>
#include <string>

namespace engine {
class IGame;
namespace ecs {
class World;
}
}

namespace editor {

// The half of Play and Stop that needs the real presentation and the editor's panels. The editor
// implements it over EngineHost and EditorPanels (EngineHostPlay); the play-session test implements it
// with a recorder.
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
    // Attaches the Inspector and Profiler panels to the game world (the world of kPrimaryWindow).
    virtual void attach_tools(engine::ecs::World& game_world) = 0;
    // Detaches the panels and drops what they copied from the game. The first step of Stop.
    virtual void detach_tools() = 0;
};

}
