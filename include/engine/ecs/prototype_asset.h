#pragma once

// docs/tech/modules/ECS.md

#include <engine/ecs/world_asset.h>
#include <engine/resources/asset_id.h>

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine {
class AssetsDb;
}

namespace engine::ecs {
class World;
class WorldSerializer;

enum class PrototypeErrorKind {
    InvalidToml,
    UnsupportedVersion,
    InvalidEntity,
    DuplicateEntityId,
    UnknownParent,
    HierarchyCycle,
    InvalidField,
    AssetError,
    PrototypeCycle,
};

[[nodiscard]] constexpr std::string_view to_string(PrototypeErrorKind kind) noexcept {
    switch (kind) {
        case PrototypeErrorKind::InvalidToml:
            return "InvalidToml";
        case PrototypeErrorKind::UnsupportedVersion:
            return "UnsupportedVersion";
        case PrototypeErrorKind::InvalidEntity:
            return "InvalidEntity";
        case PrototypeErrorKind::DuplicateEntityId:
            return "DuplicateEntityId";
        case PrototypeErrorKind::UnknownParent:
            return "UnknownParent";
        case PrototypeErrorKind::HierarchyCycle:
            return "HierarchyCycle";
        case PrototypeErrorKind::InvalidField:
            return "InvalidField";
        case PrototypeErrorKind::AssetError:
            return "AssetError";
        case PrototypeErrorKind::PrototypeCycle:
            return "PrototypeCycle";
    }
    return "Unknown";
}

struct PrototypeLoadError {
    PrototypeErrorKind kind = PrototypeErrorKind::InvalidToml;
    std::string message;
};

struct PrototypeAsset {
    int version = 1;
    std::optional<AssetId> base;
    std::vector<EntityRecord> entities;

    bool operator==(const PrototypeAsset&) const = default;
};

struct PrototypeInstance {
    AssetId prototype{};
    std::string entity_sub_id;

    bool operator==(const PrototypeInstance&) const = default;
};

[[nodiscard]] std::expected<PrototypeAsset, PrototypeLoadError> parse_prototype_asset(std::string_view toml_text);
[[nodiscard]] std::string serialize_prototype_asset(const PrototypeAsset& asset);
[[nodiscard]] std::expected<PrototypeAsset, PrototypeLoadError> resolve_prototype_asset(
        const PrototypeAsset& asset, AssetsDb* assets);

struct SpawnPrototypeParams {
    std::optional<glm::vec3> position{};
    std::optional<glm::quat> rotation{};
    std::optional<glm::vec3> scale{};
    Entity parent{};
    std::string name = "prototype";
};

[[nodiscard]] std::expected<Entity, WorldLoadError> spawn_prototype(
        World& world,
        AssetId prototype_id,
        AssetsDb& assets,
        const SpawnPrototypeParams& params = SpawnPrototypeParams{},
        const WorldSerializer* serializer = nullptr);

} // namespace engine::ecs
