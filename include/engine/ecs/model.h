#pragma once

// docs/tech/modules/ECS.md (models)

#include <engine/ecs/world_asset.h>
#include <engine/render/model.h>
#include <engine/resources/asset_id.h>

#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace engine {
class AssetsDb;
}

namespace engine::ecs {

class World;

// Marks entities `spawn_model` created under a model root. World save skips them, so loading the root's `model`
// component expands the hierarchy once instead of duplicating it.
struct ModelPartTag {};

// Serialized on the root. `instantiate_world` expands it through `spawn_model`.
struct ModelPlacement {
    AssetId model{};
    std::string clip;
};

struct SpawnModelParams {
    std::optional<glm::vec3> position{};
    std::optional<glm::quat> rotation{};
    std::optional<glm::vec3> scale{};
    Entity parent{};
    std::string name = "model";
    std::string clip;
};

// Loads `id` and spawns the hierarchy under a new root. Shaders come from the builtin lit, unlit, and skin shaders.
[[nodiscard]] std::expected<Entity, WorldLoadError> spawn_model(
        World& world, AssetId id, AssetsDb& assets, const SpawnModelParams& params = {});

// Expands a root that already has `ModelPlacement`. `instantiate_world` calls this after the root's Transform is read.
[[nodiscard]] std::expected<void, WorldLoadError> expand_model(World& world, Entity root, AssetsDb& assets);

// Global matrix of the named node, including the root's place in the world. Empty when `root` has no animator or no
// node of that name. The pose is whatever `tick_model_animator` last wrote.
[[nodiscard]] std::optional<glm::mat4> joint_world(const World& world, Entity root, std::string_view name);

// Name of the node whose posed world position is closest to `world_point`. Empty when `root` has no posed animator.
// Equal distances keep the earlier node.
[[nodiscard]] std::optional<std::string> nearest_joint(const World& world, Entity root, const glm::vec3& world_point);

// Writes each driving part's local Transform from the animator's glTF globals. Called after `tick_model_animator`.
void apply_model_poses(World& world, const render::ModelAnimator& animator);

// Sent by `run_model_animations` when the active clip's clock crosses a marker. The game reads it on the same frame
// and latches whatever the simulation should notice; a fixed step does not read this queue.
struct ModelAnimEvent {
    Entity entity{};
    std::string clip;
    std::string name;
};

}
