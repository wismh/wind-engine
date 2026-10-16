#pragma once

// docs/tech/modules/ECS.md

#include <engine/ecs/world_io.h>

#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace engine::ecs {

enum class WorldErrorKind {
    InvalidToml,
    UnsupportedVersion,
    InvalidEntity,
    DuplicateEntityId,
    UnknownParent,
    HierarchyCycle,
    UnknownActiveCamera,
    UnknownComponent,
    InvalidField,
    AssetError,
};

[[nodiscard]] constexpr std::string_view to_string(WorldErrorKind kind) noexcept {
    switch (kind) {
        case WorldErrorKind::InvalidToml:
            return "InvalidToml";
        case WorldErrorKind::UnsupportedVersion:
            return "UnsupportedVersion";
        case WorldErrorKind::InvalidEntity:
            return "InvalidEntity";
        case WorldErrorKind::DuplicateEntityId:
            return "DuplicateEntityId";
        case WorldErrorKind::UnknownParent:
            return "UnknownParent";
        case WorldErrorKind::HierarchyCycle:
            return "HierarchyCycle";
        case WorldErrorKind::UnknownActiveCamera:
            return "UnknownActiveCamera";
        case WorldErrorKind::UnknownComponent:
            return "UnknownComponent";
        case WorldErrorKind::InvalidField:
            return "InvalidField";
        case WorldErrorKind::AssetError:
            return "AssetError";
    }
    return "Unknown";
}

struct WorldLoadError {
    WorldErrorKind kind = WorldErrorKind::InvalidToml;
    std::string message;
};

struct EntityRecord {
    std::string id;
    std::string parent;
    std::map<std::string, FieldTable, std::less<>> components;

    bool operator==(const EntityRecord&) const = default;
};

struct WorldAsset {
    int version = 1;
    std::string active_camera;
    std::vector<EntityRecord> entities;

    bool operator==(const WorldAsset&) const = default;
};

[[nodiscard]] std::expected<WorldAsset, WorldLoadError> parse_world_asset(std::string_view toml_text);
[[nodiscard]] std::string serialize_world_asset(const WorldAsset& asset);

} // namespace engine::ecs
