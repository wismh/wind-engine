#pragma once

#include "play_host.h"

#include <engine/core/game_module.h>

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace engine {
class AssetsDb;
namespace ecs {
class World;
class WorldSerializer;
} // namespace ecs
} // namespace engine

namespace editor {

// The project's game module and catalog loaded for Edit Mode between plays, so the editor has the game's
// WIND_COMPONENT serializers, WIND_SERIALIZABLE loaders, and assets before Play starts and again after Stop.
// Unloaded while PlaySession owns the module and catalog.
class EditModule {
public:
    EditModule(IPlayHost& host, engine::AssetsDb& assets, std::filesystem::path live_root);
    ~EditModule();

    EditModule(const EditModule&) = delete;
    EditModule& operator=(const EditModule&) = delete;

    // Unloads any previous edit module, loads a live copy of `module` and `<module dir>/assets/catalog.toml`, and
    // remembers `module` as the last module path. An error leaves no module or catalog loaded.
    [[nodiscard]] std::expected<void, std::string> load(const std::filesystem::path& module);

    // Resets every world serializer registered from this module (and `edit_world`'s serializer when given) to the
    // built-in engine components before unloading the catalog and freeing the module copy. Keeps `module_path()` so
    // Stop can reload the module after Play.
    void unload(engine::ecs::World* edit_world = nullptr);

    // Unloads the module and clears the remembered `module_path()`. Used when opening another project.
    void reset(engine::ecs::World* edit_world = nullptr);

    // Records the path of a newly built module so Stop can load it for Edit Mode even when the build went straight
    // to Play.
    void remember_module(std::filesystem::path module);

    // Resets `serializer` (or `world.ctx<WorldSerializer>()`) to the built-in engine components and, when a module
    // is loaded, registers the game's components on it. A `World` passed to `register_components(world)` is tracked
    // with a weak anchor so `unload()` resets its serializer before freeing the module if the world is still alive.
    void register_components(engine::ecs::WorldSerializer& serializer) const;
    void register_components(engine::ecs::World& world) const;

    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] const std::filesystem::path& module_path() const noexcept;

private:
    IPlayHost* host_;
    engine::AssetsDb* assets_;
    std::filesystem::path live_root_;
    std::optional<engine::GameModule> module_;
    std::filesystem::path assets_dir_;
    std::filesystem::path last_module_path_;
    mutable std::vector<std::weak_ptr<engine::ecs::WorldSerializer*>> tracked_worlds_;
};

}
