#include "hierarchy_commands.h"

#include <engine/ecs/camera.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_io.h>
#include <engine/ecs/world_serializer.h>
#include <engine/resources/assets_db.h>

namespace editor {

EntitySubtreeSnapshot capture_subtree(
        const engine::ecs::World& world, engine::ecs::Entity entity, const engine::ecs::WorldSerializer* serializer) {
    EntitySubtreeSnapshot snap;
    if (const auto* name = world.try_get<engine::ecs::EntityName>(entity)) {
        snap.name = name->id;
    }
    if (const auto* tr = world.try_get<engine::Transform>(entity)) {
        snap.transform = *tr;
    }
    if (serializer != nullptr) {
        for (const auto& [comp_name, entry] : serializer->entries()) {
            engine::ecs::FieldTable table;
            engine::ecs::ComponentWriter writer(table);
            if (entry.write(world, entity, writer)) {
                snap.components.emplace(comp_name, std::move(table));
            }
        }
    }
    if (const auto* children = world.try_get<engine::Children>(entity)) {
        for (const engine::ecs::Entity child : children->entities) {
            if (world.valid(child)) {
                snap.children.push_back(capture_subtree(world, child, serializer));
            }
        }
    }
    return snap;
}

engine::ecs::Entity restore_subtree(engine::ecs::World& world, const EntitySubtreeSnapshot& snap,
        std::optional<engine::ecs::Entity> parent, const engine::ecs::WorldSerializer* serializer,
        engine::AssetsDb* assets) {
    const engine::ecs::Entity entity = world.create();
    if (!snap.name.empty()) {
        world.emplace<engine::ecs::EntityName>(entity, engine::ecs::EntityName{.id = snap.name});
    }
    if (snap.transform) {
        world.emplace<engine::Transform>(entity, *snap.transform);
    }
    if (serializer != nullptr) {
        for (const auto& [comp_name, table] : snap.components) {
            if (const auto* entry = serializer->find(comp_name)) {
                engine::ecs::ComponentReader reader(table, {}, assets);
                (void)entry->read(world, entity, reader);
            }
        }
    }
    if (parent && world.valid(*parent)) {
        (void)engine::ecs::set_parent(world, entity, *parent, engine::ecs::KeepWorld::No);
    }
    for (const auto& child_snap : snap.children) {
        restore_subtree(world, child_snap, entity, serializer, assets);
    }
    return entity;
}

void destroy_subtree(engine::ecs::World& world, engine::ecs::Entity entity) {
    if (!world.valid(entity)) {
        return;
    }
    if (const auto* children = world.try_get<engine::Children>(entity)) {
        const std::vector<engine::ecs::Entity> kids = children->entities;
        for (const engine::ecs::Entity kid : kids) {
            destroy_subtree(world, kid);
        }
    }
    engine::ecs::clear_parent(world, entity, engine::ecs::KeepWorld::No);
    world.destroy(entity);
}

// --- EntityRenameCommand ---

EntityRenameCommand::EntityRenameCommand(
        engine::ecs::World* world, engine::ecs::Entity entity, std::string before, std::string after)
    : world_(world), entity_(entity), before_(std::move(before)), after_(std::move(after)) {}

bool EntityRenameCommand::undo() {
    if (world_ == nullptr || !world_->valid(entity_)) {
        return true;
    }
    if (before_.empty()) {
        world_->remove<engine::ecs::EntityName>(entity_);
    } else if (auto* name = world_->try_get<engine::ecs::EntityName>(entity_)) {
        name->id = before_;
    } else {
        world_->emplace<engine::ecs::EntityName>(entity_, engine::ecs::EntityName{.id = before_});
    }
    return true;
}

bool EntityRenameCommand::redo() {
    if (world_ == nullptr || !world_->valid(entity_)) {
        return true;
    }
    if (after_.empty()) {
        world_->remove<engine::ecs::EntityName>(entity_);
    } else if (auto* name = world_->try_get<engine::ecs::EntityName>(entity_)) {
        name->id = after_;
    } else {
        world_->emplace<engine::ecs::EntityName>(entity_, engine::ecs::EntityName{.id = after_});
    }
    return true;
}

// --- EntityReparentCommand ---

EntityReparentCommand::EntityReparentCommand(engine::ecs::World* world, engine::ecs::Entity child,
        std::optional<engine::ecs::Entity> old_parent, std::optional<engine::ecs::Entity> new_parent,
        std::optional<engine::Transform> old_transform, std::optional<engine::Transform> new_transform)
    : world_(world)
    , child_(child)
    , old_parent_(old_parent)
    , new_parent_(new_parent)
    , old_transform_(old_transform)
    , new_transform_(new_transform) {}

bool EntityReparentCommand::undo() {
    if (world_ == nullptr || !world_->valid(child_)) {
        return true;
    }
    if (old_parent_ && world_->valid(*old_parent_)) {
        (void)engine::ecs::set_parent(*world_, child_, *old_parent_, engine::ecs::KeepWorld::No);
    } else {
        engine::ecs::clear_parent(*world_, child_, engine::ecs::KeepWorld::No);
    }
    if (old_transform_ && world_->try_get<engine::Transform>(child_)) {
        *world_->try_get<engine::Transform>(child_) = *old_transform_;
    }
    engine::ecs::update_world_transforms(*world_);
    return true;
}

bool EntityReparentCommand::redo() {
    if (world_ == nullptr || !world_->valid(child_)) {
        return true;
    }
    if (new_parent_ && world_->valid(*new_parent_)) {
        (void)engine::ecs::set_parent(*world_, child_, *new_parent_, engine::ecs::KeepWorld::No);
    } else {
        engine::ecs::clear_parent(*world_, child_, engine::ecs::KeepWorld::No);
    }
    if (new_transform_ && world_->try_get<engine::Transform>(child_)) {
        *world_->try_get<engine::Transform>(child_) = *new_transform_;
    }
    engine::ecs::update_world_transforms(*world_);
    return true;
}

// --- EntityCreateCommand ---

EntityCreateCommand::EntityCreateCommand(engine::ecs::World* world, engine::ecs::Entity entity,
        std::optional<engine::ecs::Entity> parent, const engine::ecs::WorldSerializer* serializer,
        engine::AssetsDb* assets)
    : world_(world)
    , entity_(entity)
    , parent_(parent)
    , serializer_(serializer)
    , assets_(assets)
    , snapshot_(capture_subtree(*world, entity, serializer)) {}

bool EntityCreateCommand::undo() {
    if (world_ == nullptr || !world_->valid(entity_)) {
        return true;
    }
    destroy_subtree(*world_, entity_);
    entity_ = {};
    return true;
}

bool EntityCreateCommand::redo() {
    if (world_ == nullptr) {
        return true;
    }
    entity_ = restore_subtree(*world_, snapshot_, parent_, serializer_, assets_);
    return true;
}

// --- EntityDeleteCommand ---

EntityDeleteCommand::EntityDeleteCommand(engine::ecs::World* world, engine::ecs::Entity entity,
        std::optional<engine::ecs::Entity> parent, const engine::ecs::WorldSerializer* serializer,
        engine::AssetsDb* assets)
    : world_(world)
    , entity_(entity)
    , parent_(parent)
    , serializer_(serializer)
    , assets_(assets)
    , snapshot_(capture_subtree(*world, entity, serializer)) {
    destroy_subtree(*world_, entity_);
}

bool EntityDeleteCommand::undo() {
    if (world_ == nullptr) {
        return true;
    }
    entity_ = restore_subtree(*world_, snapshot_, parent_, serializer_, assets_);
    return true;
}

bool EntityDeleteCommand::redo() {
    if (world_ == nullptr || !world_->valid(entity_)) {
        return true;
    }
    destroy_subtree(*world_, entity_);
    entity_ = {};
    return true;
}

}
