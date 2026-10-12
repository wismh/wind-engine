#pragma once

// docs/tech/features/UI Input.md

#include <engine/core/input_system.h>
#include <engine/core/window_desc.h>
#include <engine/ecs/entity.h>
#include <engine/ecs/world.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <optional>

namespace engine {

// Makes an entity in the world something the mouse can hover and click, like a UI button. The input system tests the
// cursor against every enabled target, reports the topmost one under it, and counts it for click-through: a game no
// longer hit-tests sprites by hand or writes into `Presentation::mouse`.
//
// The rectangle is in the entity's own space, so it follows its Transform (move, rotate, scale, parent). Camera pan
// and zoom are applied to the cursor, and each window uses its own size, so the same entity can sit under the cursor
// in one window and not in another.
struct PointerTarget {
    // The rectangle in world units, with `pivot` as its origin (the same meaning as `Sprite::pivot`: {0.5, 0.5} is
    // the centre, {0, 0} the bottom-left corner). {0, 0} derives both from the entity's `Sprite`: `pixel_size /
    // pixels_per_unit` at the sprite's pivot. A target with no size and no sprite size is never hit.
    glm::vec2 size{0.0f, 0.0f};
    glm::vec2 pivot{0.5f, 0.5f};

    // Which of two overlapping targets is on top. The entity's Sprite `layer` and `order_in_layer` when it has a
    // Sprite (the order it is drawn in), otherwise these. A tie goes to the later entity, as in the draw order.
    int layer = 0;
    int order_in_layer = 0;

    // A disabled target is invisible to the pointer. Set false to take a button out without removing the component.
    bool enabled = true;
    // Only this window's cursor can hit it. Unset: any window the world shows.
    std::optional<WindowId> window;

    // Written by the input system, read by the game: the cursor is over this target (and no UI is), and a button
    // went down on it and is still held. Both are false for a disabled target.
    bool hovered = false;
    bool pressed = false;
};

// What happened to a target, sent every frame through `ecs::EventWriter<PointerTargetEvent>` (read it with an
// `EventReader` like any other event).
struct PointerTargetEvent {
    enum class Kind : std::uint8_t {
        Enter,   // the cursor came onto the target
        Exit,    // and left it: also when UI came over it, or it was disabled
        Press,   // a button went down on it
        Click,   // that same button came up on the same target
    };

    Kind kind = Kind::Enter;
    ecs::Entity entity{};
    WindowId window = kPrimaryWindow;
    // Press and Click: which button. None for Enter and Exit.
    MouseButton button = MouseButton::None;
    // The cursor in world units (x, y), for every kind.
    glm::vec2 world_position{0.0f, 0.0f};
};

// The topmost enabled target under `screen` (window client pixels, the same space as `MouseEvent::position`) in
// `window`, using the world's `ActiveCamera` and that window's size. nullopt when there is none, no valid camera, or
// the window has no size yet. Pure geometry: it knows nothing about UI. The input system calls it after the UI has
// had its turn.
[[nodiscard]] std::optional<ecs::Entity> pointer_target_at(ecs::World& world, WindowId window, glm::vec2 screen);

}
