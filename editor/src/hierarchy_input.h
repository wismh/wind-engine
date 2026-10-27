#pragma once

#include "hierarchy_panel.h"

#include <engine/core/input_system.h>
#include <engine/ecs/entity.h>
#include <engine/ecs/events.h>
#include <engine/ecs/world.h>

#include <glm/vec2.hpp>

#include <optional>

namespace editor {

// The Hierarchy tab's pointer and keyboard input that the tab's own view-model cannot see: entity drag & drop
// (reparenting onto an entity, or dropping on empty space to move to root), rename focus, and shortcuts (F2, Del, Ctrl+N).
class HierarchyInput {
public:
    explicit HierarchyInput(HierarchyPanel& hierarchy) : hierarchy_(&hierarchy) {}

    // Puts keyboard focus in the rename field once the row has been built.
    void focus_rename(engine::ecs::World& world, engine::ecs::Entity canvas_entity);
    // A press on a row arms a drag; it becomes a reparent once the pointer travels, and a release drops.
    void read_drag(engine::ecs::World& world, engine::ecs::Entity canvas_entity);
    // True when `event` was a Hierarchy shortcut and has been applied. Auto-repeat never is.
    [[nodiscard]] bool handle_key(const engine::KeyEvent& event, bool ctrl, bool shift);

private:
    [[nodiscard]] std::optional<engine::ecs::Entity> row_at(engine::ecs::World& world, engine::ecs::Entity canvas_entity,
            glm::vec2 window_point) const;

    HierarchyPanel* hierarchy_;
    engine::ecs::EventCursor<engine::MouseEvent> mouse_cursor_;
    bool drag_armed_ = false;
    bool drag_active_ = false;
    engine::ecs::Entity drag_entity_{};
    glm::vec2 drag_origin_{};
    std::optional<std::optional<engine::ecs::Entity>> drop_target_;
};

}
