#include "canvas_pointer.h"

#include <engine/ui/canvas.h>
#include <engine/ui/presentation.h>

#include <glm/vec2.hpp>

#include <optional>

namespace editor {

engine::ecs::Entity top_canvas_at_pointer(engine::ecs::World& world, engine::WindowId window) {
    const glm::vec2 pointer = engine::ui::pointer_for(world, window).position;
    engine::ecs::Entity top{};
    std::optional<int> top_order;
    auto view = world.view<engine::ui::UiCanvas>();
    for (const engine::ecs::Entity entity : view) {
        const engine::ui::UiCanvas& canvas = view.get<engine::ui::UiCanvas>(entity);
        if (canvas.window != window || !engine::ui::rect_contains(canvas.rect, pointer.x, pointer.y)) {
            continue;
        }
        if (!top_order || canvas.order > *top_order) {
            top = entity;
            top_order = canvas.order;
        }
    }
    return top;
}

}
