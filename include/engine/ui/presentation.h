#pragma once

#include <engine/ui/canvas.h>

namespace engine::ui {

// Process-owned window state shared by every world that draws UI or meshes.
// Sizes, the pointer, and mouse consumption are per window, not per simulation.
struct Presentation {
    WindowSizes sizes;
    MouseConsumed mouse;
    UiPointer pointer;
    UiPointers pointers;
};

void bind_presentation(ecs::World& world, Presentation& presentation);

// Fatal when this world was never bound. The report goes through `ctx<IFatalError*>` when set.
[[nodiscard]] Presentation& presentation_of(ecs::World& world);

void reset_pointer_frame(Presentation& presentation);

}
