#pragma once

#include "explorer_panel.h"

#include <engine/core/input_system.h>
#include <engine/ecs/entity.h>
#include <engine/ecs/events.h>
#include <engine/ecs/world.h>

#include <glm/vec2.hpp>

#include <optional>
#include <string>

namespace editor {

// The Project tab's pointer and keyboard input that the tab's own view-model cannot see: the row drag that moves a
// file into a folder, the focus of the rename field, and the Project shortcuts (F2, Ctrl+Shift+N, Ctrl+C / X / V,
// Delete). `EditorPanels` feeds it the frame's events and tells it which canvas is the Project tab.
class ExplorerInput {
public:
    explicit ExplorerInput(ExplorerPanel& explorer) : explorer_(&explorer) {}

    // Puts keyboard focus in the rename field once the row has been built.
    void focus_rename(engine::ecs::World& world, engine::ecs::Entity canvas_entity);
    // A press on a row arms a drag; it becomes a move once the pointer travels, and a release over a folder drops.
    void read_drag(engine::ecs::World& world, engine::ecs::Entity canvas_entity);
    // True when `event` was a Project shortcut and has been applied. Auto-repeat never is.
    [[nodiscard]] bool handle_key(const engine::KeyEvent& event, bool ctrl, bool shift);

private:
    [[nodiscard]] std::string row_at(engine::ecs::World& world, engine::ecs::Entity canvas_entity,
            glm::vec2 window_point) const;

    ExplorerPanel* explorer_;
    engine::ecs::EventCursor<engine::MouseEvent> mouse_cursor_;
    // A press on a project row that becomes a move once the pointer travels.
    bool drag_armed_ = false;
    bool drag_active_ = false;
    std::string drag_key_;
    glm::vec2 drag_origin_{};
    std::optional<std::string> drag_folder_;
};

}
