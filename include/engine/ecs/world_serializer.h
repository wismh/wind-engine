#pragma once

// docs/tech/modules/ECS.md

#include <engine/ecs/world.h>
#include <engine/ecs/world_asset.h>
#include <engine/ecs/world_io.h>

#include <expected>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::ecs {

struct InstantiatedWorld {
    std::vector<Entity> entities;
    std::unordered_map<std::string, Entity> by_id;
};

// Registry of component serializers (built-in engine components + game components registered via `component_io.h`).
// Can be passed explicitly or stored in `world.ctx<WorldSerializer>()` so nothing from `game.dll` outlives the game's
// worlds when the editor stops.
class WorldSerializer {
public:
    struct Entry {
        std::string name;
        std::function<bool(const World&, Entity, ComponentWriter&)> write;
        std::function<bool(World&, Entity, ComponentReader&)> read;
        // Drops the component when the entity has one. False when it does not. Empty when the entry cannot remove.
        std::function<bool(World&, Entity)> remove;
    };

    WorldSerializer();

    template<typename T, typename WriteFn, typename ReadFn>
    void register_component(std::string_view name, WriteFn&& write_fn, ReadFn&& read_fn) {
        std::string key(name);
        Entry entry;
        entry.name = key;
        entry.write = [w = std::forward<WriteFn>(write_fn)](
                              const World& world, Entity entity, ComponentWriter& writer) -> bool {
            const T* comp = world.try_get<T>(entity);
            if (comp == nullptr) {
                return false;
            }
            w(writer, *comp);
            return true;
        };
        entry.read = [r = std::forward<ReadFn>(read_fn)](
                             World& world, Entity entity, ComponentReader& reader) -> bool {
            T comp{};
            if (const T* existing = world.try_get<T>(entity)) {
                comp = *existing;
            }
            r(reader, comp);
            if (!reader.ok()) {
                return false;
            }
            if (T* existing = world.try_get<T>(entity)) {
                *existing = std::move(comp);
            } else {
                world.emplace<T>(entity, std::move(comp));
            }
            return true;
        };
        entry.remove = [](World& world, Entity entity) -> bool {
            if (world.try_get<T>(entity) == nullptr) {
                return false;
            }
            world.remove<T>(entity);
            return true;
        };
        entries_.insert_or_assign(std::move(key), std::move(entry));
    }

    [[nodiscard]] const Entry* find(std::string_view name) const;
    [[nodiscard]] const std::map<std::string, Entry, std::less<>>& entries() const noexcept { return entries_; }

private:
    std::map<std::string, Entry, std::less<>> entries_;
};

struct UnknownComponents {
    std::map<std::string, FieldTable, std::less<>> components;
};

enum class UnknownComponentMode {
    Reject,
    Preserve,
};

[[nodiscard]] std::expected<InstantiatedWorld, WorldLoadError> instantiate_world(
        World& world, const WorldAsset& asset, AssetsDb* assets = nullptr, const WorldSerializer* serializer = nullptr,
        UnknownComponentMode unknown_mode = UnknownComponentMode::Reject);

[[nodiscard]] std::expected<InstantiatedWorld, WorldLoadError> load_world_from_toml(
        World& world, std::string_view toml_text, AssetsDb* assets = nullptr,
        const WorldSerializer* serializer = nullptr,
        UnknownComponentMode unknown_mode = UnknownComponentMode::Reject);

[[nodiscard]] WorldAsset capture_world_asset(const World& world, const WorldSerializer* serializer = nullptr);

[[nodiscard]] std::string save_world_to_toml(const World& world, const WorldSerializer* serializer = nullptr);

} // namespace engine::ecs
