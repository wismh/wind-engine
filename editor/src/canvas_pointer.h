#pragma once

#include <engine/core/window_desc.h>
#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>

namespace editor {

// The canvas of the window that is topmost under the pointer, or a null entity when the pointer is over none.
[[nodiscard]] engine::ecs::Entity top_canvas_at_pointer(engine::ecs::World& world, engine::WindowId window);

}
