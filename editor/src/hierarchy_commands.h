#pragma once

#include "command_history.h"

#include <engine/ecs/entity.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_io.h>
#include <engine/ecs/world_serializer.h>

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace engine {
class AssetsDb;
}

namespace editor {

struct EntitySubtreeSnapshot {
    std::string name;
    std::optional<engine::Transform> transform;
    std::map<std::string, engine::ecs::FieldTable> components;
    std::vector<EntitySubtreeSnapshot> children;
};

[[nodiscard]] EntitySubtreeSnapshot capture_subtree(
        const engine::ecs::World& world, engine::ecs::Entity entity, const engine::ecs::WorldSerializer* serializer);

engine::ecs::Entity restore_subtree(engine::ecs::World& world, const EntitySubtreeSnapshot& snap,
        std::optional<engine::ecs::Entity> parent, const engine::ecs::WorldSerializer* serializer,
        engine::AssetsDb* assets);

void destroy_subtree(engine::ecs::World& world, engine::ecs::Entity entity);

class EntityRenameCommand final : public EditorCommand {
public:
    EntityRenameCommand(engine::ecs::World* world, engine::ecs::Entity entity, std::string before, std::string after);

    bool undo() override;
    bool redo() override;
    [[nodiscard]] Kind kind() const override { return Kind::Entity; }

private:
    engine::ecs::World* world_ = nullptr;
    engine::ecs::Entity entity_{};
    std::string before_;
    std::string after_;
};

class EntityReparentCommand final : public EditorCommand {
public:
    EntityReparentCommand(engine::ecs::World* world, engine::ecs::Entity child,
            std::optional<engine::ecs::Entity> old_parent, std::optional<engine::ecs::Entity> new_parent,
            std::optional<engine::Transform> old_transform, std::optional<engine::Transform> new_transform);

    bool undo() override;
    bool redo() override;
    [[nodiscard]] Kind kind() const override { return Kind::Entity; }

private:
    engine::ecs::World* world_ = nullptr;
    engine::ecs::Entity child_{};
    std::optional<engine::ecs::Entity> old_parent_;
    std::optional<engine::ecs::Entity> new_parent_;
    std::optional<engine::Transform> old_transform_;
    std::optional<engine::Transform> new_transform_;
};

class EntityCreateCommand final : public EditorCommand {
public:
    EntityCreateCommand(engine::ecs::World* world, engine::ecs::Entity entity,
            std::optional<engine::ecs::Entity> parent, const engine::ecs::WorldSerializer* serializer,
            engine::AssetsDb* assets);

    bool undo() override;
    bool redo() override;
    [[nodiscard]] Kind kind() const override { return Kind::Entity; }
    [[nodiscard]] engine::ecs::Entity entity() const noexcept { return entity_; }

private:
    engine::ecs::World* world_ = nullptr;
    engine::ecs::Entity entity_{};
    std::optional<engine::ecs::Entity> parent_;
    const engine::ecs::WorldSerializer* serializer_ = nullptr;
    engine::AssetsDb* assets_ = nullptr;
    EntitySubtreeSnapshot snapshot_;
};

class EntityDeleteCommand final : public EditorCommand {
public:
    EntityDeleteCommand(engine::ecs::World* world, engine::ecs::Entity entity,
            std::optional<engine::ecs::Entity> parent, const engine::ecs::WorldSerializer* serializer,
            engine::AssetsDb* assets);

    bool undo() override;
    bool redo() override;
    [[nodiscard]] Kind kind() const override { return Kind::Entity; }
    [[nodiscard]] engine::ecs::Entity entity() const noexcept { return entity_; }

private:
    engine::ecs::World* world_ = nullptr;
    engine::ecs::Entity entity_{};
    std::optional<engine::ecs::Entity> parent_;
    const engine::ecs::WorldSerializer* serializer_ = nullptr;
    engine::AssetsDb* assets_ = nullptr;
    EntitySubtreeSnapshot snapshot_;
};

}
