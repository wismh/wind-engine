#pragma once

#include <engine/ui/canvas.h>

namespace engine::ui {

// Process-owned window state shared by every world that draws UI or meshes.
// Sizes, the pointer, the cursor, and mouse consumption are per window, not per simulation.
struct Presentation {
    WindowSizes sizes;
    MouseConsumed mouse;
    UiPointer pointer;
    UiPointers pointers;
    UiCursors cursors;
};

void bind_presentation(ecs::World& world, Presentation& presentation);

// A world owned by Worlds (its `ctx<IFatalError*>` is set) must have been bound: otherwise this reports fatally.
// A bare World, with no IFatalError, owns its Presentation in `ctx<Presentation>()`.
[[nodiscard]] Presentation& presentation_of(ecs::World& world);

void reset_pointer_frame(Presentation& presentation);

}
