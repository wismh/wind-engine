#pragma once

#include "command_history.h"

#include <engine/ecs/entity.h>
#include <engine/ecs/world_io.h>

#include <string>
#include <string_view>

namespace engine {
class AssetsDb;
}

namespace engine::ecs {
class World;
}

namespace editor {

// Writes the component's table the same way a world save does, so a later `read_component` of that table puts the
// component back. False when the entity does not have it or the name is not registered.
[[nodiscard]] bool write_component(const engine::ecs::World& world, engine::ecs::Entity entity,
        std::string_view name, engine::ecs::FieldTable& out);

// Applies `table` through the component's reader. An empty table adds the component with its C++ defaults.
[[nodiscard]] bool read_component(engine::ecs::World& world, engine::ecs::Entity entity, std::string_view name,
        const engine::ecs::FieldTable& table, engine::AssetsDb* assets);

[[nodiscard]] bool erase_component(engine::ecs::World& world, engine::ecs::Entity entity, std::string_view name);

// Replaces one field of a table copied from `write_component`. `slot` below 0 is the whole value (a string or a
// number). Zero and up is one element of a short numeric array. `rotation` is stored as a quaternion and edited
// as Euler degrees on slots 0..2. A 4-component `color` edits rgb and keeps alpha. False when the text is not
// that value: the table is left unchanged.
[[nodiscard]] bool assign_text(engine::ecs::FieldTable& table, std::string_view key, int slot, std::string_view text);

[[nodiscard]] bool assign_bool(engine::ecs::FieldTable& table, std::string_view key, bool value);

// One inspector edit of a registered component. The change is already applied; undo and redo apply `before` or
// `after` again. `coalesce` ties keystrokes of one field into a single step. An empty key never merges.
class EntityEditCommand final : public EditorCommand {
public:
    enum class Change { Set, Add, Remove };

    EntityEditCommand(engine::ecs::World* world, engine::AssetsDb* assets, engine::ecs::Entity entity,
            std::string component, Change change, engine::ecs::FieldTable before, engine::ecs::FieldTable after,
            std::string coalesce);

    bool undo() override;
    bool redo() override;
    [[nodiscard]] Kind kind() const override { return Kind::Entity; }
    [[nodiscard]] bool merges_with(const EditorCommand& other) const override;
    void merge_from(EditorCommand& newer) override;

private:
    [[nodiscard]] bool apply(bool forward);

    engine::ecs::World* world_ = nullptr;
    engine::AssetsDb* assets_ = nullptr;
    engine::ecs::Entity entity_{};
    std::string component_;
    Change change_ = Change::Set;
    engine::ecs::FieldTable before_;
    engine::ecs::FieldTable after_;
    std::string coalesce_;
};

}
