#include <engine/ecs/pointer_target.h>

#include <engine/ecs/camera.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/render/sprite.h>
#include <engine/ui/canvas.h>
#include <engine/ui/presentation.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace engine {

namespace {

// What run_pointer_targets remembers between frames, per world.
struct PointerTargetState {
    ecs::EventCursor<MouseEvent> mouse;
    std::vector<WindowId> windows;
    std::unordered_map<WindowId, ecs::Entity> hovered;
    struct Press {
        ecs::Entity entity{};
        MouseButton button = MouseButton::None;
    };
    std::unordered_map<WindowId, Press> pressed;
};

struct Rect2 {
    glm::vec2 min;
    glm::vec2 max;
};

// The rectangle in the entity's space, or nullopt when the target has no size.
std::optional<Rect2> local_rect(const PointerTarget& target, const render::Sprite* sprite) {
    glm::vec2 size = target.size;
    glm::vec2 pivot = target.pivot;
    if (size.x <= 0.0f || size.y <= 0.0f) {
        if (sprite == nullptr || sprite->pixel_size.x <= 0.0f || sprite->pixel_size.y <= 0.0f ||
                sprite->pixels_per_unit <= 0.0f) {
            return std::nullopt;
        }
        size = sprite->pixel_size / sprite->pixels_per_unit;
        pivot = sprite->pivot;
    }
    const glm::vec2 min{-pivot.x * size.x, -pivot.y * size.y};
    return Rect2{min, min + size};
}

struct Candidate {
    ecs::Entity entity{};
    int layer = 0;
    int order_in_layer = 0;
};

bool above(const Candidate& a, const Candidate& b) {
    if (a.layer != b.layer) {
        return a.layer > b.layer;
    }
    if (a.order_in_layer != b.order_in_layer) {
        return a.order_in_layer > b.order_in_layer;
    }
    return a.entity.index > b.entity.index;
}

struct CameraView {
    const Camera* camera = nullptr;
    glm::mat4 camera_world{1.0f};
    ui::WindowSize size;
};

std::optional<CameraView> camera_view(ecs::World& world, WindowId window) {
    const ecs::Entity camera_entity = world.ctx<ActiveCamera>().entity;
    if (!world.valid(camera_entity)) {
        return std::nullopt;
    }
    const Camera* camera = world.try_get<Camera>(camera_entity);
    if (camera == nullptr || world.try_get<Transform>(camera_entity) == nullptr) {
        return std::nullopt;
    }
    const ui::WindowSize size = ui::window_size_for(world, window);
    if (size.width <= 0 || size.height <= 0) {
        return std::nullopt;
    }
    return CameraView{camera, ecs::world_matrix(world, camera_entity), size};
}

// The cursor in world units for hit-testing a target at world z `z`.
glm::vec3 world_point(const CameraView& view, glm::vec2 screen, float z) {
    glm::vec3 point = screen_to_world(screen, *view.camera, view.camera_world, view.size);
    point.z = z;
    return point;
}

std::optional<ecs::Entity> topmost_at(ecs::World& world, WindowId window, const CameraView& view, glm::vec2 screen) {
    std::optional<Candidate> best;
    auto targets = world.view<PointerTarget, Transform>();
    for (const ecs::Entity entity : targets) {
        const PointerTarget& target = targets.get<PointerTarget>(entity);
        if (!target.enabled || (target.window && *target.window != window)) {
            continue;
        }
        const render::Sprite* sprite = world.try_get<render::Sprite>(entity);
        const std::optional<Rect2> rect = local_rect(target, sprite);
        if (!rect) {
            continue;
        }
        const glm::mat4 model = ecs::world_matrix(world, entity);
        const glm::vec3 world_at = world_point(view, screen, model[3][2]);
        const glm::vec4 local = glm::inverse(model) * glm::vec4{world_at, 1.0f};
        if (local.x < rect->min.x || local.x > rect->max.x || local.y < rect->min.y || local.y > rect->max.y) {
            continue;
        }
        const Candidate candidate{
                entity,
                sprite != nullptr ? sprite->layer : target.layer,
                sprite != nullptr ? sprite->order_in_layer : target.order_in_layer,
        };
        if (!best || above(candidate, *best)) {
            best = candidate;
        }
    }
    if (!best) {
        return std::nullopt;
    }
    return best->entity;
}

// The target the pointer is on, unless UI is. `consumed` already holds the windows whose UI took an event this frame; a
// pointer resting on UI sends no event (the UI marks a window only when it handles one), so when a target is under
// the cursor the UI is asked again at that position. That ask also puts the window into `consumed` for click-through.
std::optional<ecs::Entity> target_not_under_ui(ecs::World& world, WindowId window, glm::vec2 position) {
    ui::Presentation& presentation = ui::presentation_of(world);
    if (presentation.mouse.consumed_for(window)) {
        return std::nullopt;
    }
    const std::optional<ecs::Entity> candidate = pointer_target_at(world, window, position);
    if (!candidate) {
        return std::nullopt;
    }
    ui::update_pointer_hover(world, position.x, position.y, window);
    if (presentation.mouse.consumed_for(window)) {
        return std::nullopt;
    }
    return candidate;
}

PointerTarget* live_target(ecs::World& world, ecs::Entity entity) {
    return world.valid(entity) ? world.try_get<PointerTarget>(entity) : nullptr;
}

void send(ecs::World& world, PointerTargetEvent::Kind kind, ecs::Entity entity, WindowId window, MouseButton button,
        glm::vec2 world_position) {
    ecs::EventWriter<PointerTargetEvent>{world}.send(PointerTargetEvent{kind, entity, window, button, world_position});
}

glm::vec2 cursor_world(ecs::World& world, WindowId window, ecs::Entity entity, glm::vec2 screen) {
    const std::optional<CameraView> view = camera_view(world, window);
    if (!view) {
        return {0.0f, 0.0f};
    }
    const float z = world.valid(entity) ? ecs::world_matrix(world, entity)[3][2] : 0.0f;
    const glm::vec3 point = world_point(*view, screen, z);
    return {point.x, point.y};
}

}

std::optional<ecs::Entity> pointer_target_at(ecs::World& world, WindowId window, glm::vec2 screen) {
    const std::optional<CameraView> view = camera_view(world, window);
    if (!view) {
        return std::nullopt;
    }
    return topmost_at(world, window, *view, screen);
}

// Frame / Input, registered after run_input and run_dock_input. The UI has had its turn: a window whose
// `Presentation::mouse` already holds a UI hit this frame gives the targets nothing, and its targets are exited. A
// target under the cursor of a window that UI did not take is then counted into the same set, which is what
// click-through reads, so a game never writes `consumed_windows` for a sprite.
void run_pointer_targets(ecs::World& world) {
    PointerTargetState& state = world.ctx<PointerTargetState>();
    ui::Presentation& presentation = ui::presentation_of(world);

    for (const MouseEvent& event : ecs::EventReader<MouseEvent>{world, state.mouse}) {
        if (std::ranges::find(state.windows, event.window) == state.windows.end()) {
            state.windows.push_back(event.window);
        }
        if (event.kind != MouseEvent::Kind::Down && event.kind != MouseEvent::Kind::Up) {
            continue;
        }
        const std::optional<ecs::Entity> hit = target_not_under_ui(world, event.window, event.position);
        if (event.kind == MouseEvent::Kind::Down) {
            if (hit) {
                state.pressed[event.window] = PointerTargetState::Press{*hit, event.button};
                if (PointerTarget* target = live_target(world, *hit)) {
                    target->pressed = true;
                }
                send(world, PointerTargetEvent::Kind::Press, *hit, event.window, event.button,
                        cursor_world(world, event.window, *hit, event.position));
            }
            continue;
        }
        const auto held = state.pressed.find(event.window);
        if (held == state.pressed.end() || held->second.button != event.button) {
            continue;
        }
        const ecs::Entity pressed = held->second.entity;
        state.pressed.erase(held);
        if (PointerTarget* target = live_target(world, pressed)) {
            target->pressed = false;
        }
        if (hit && *hit == pressed) {
            send(world, PointerTargetEvent::Kind::Click, pressed, event.window, event.button,
                    cursor_world(world, event.window, pressed, event.position));
        }
    }

    for (const WindowId window : state.windows) {
        const glm::vec2 position = ui::pointer_for(world, window).position;
        const std::optional<ecs::Entity> now = target_not_under_ui(world, window, position);
        ecs::Entity previous{};
        bool had_previous = false;
        if (const auto it = state.hovered.find(window); it != state.hovered.end()) {
            previous = it->second;
            had_previous = true;
        }
        const bool unchanged = had_previous ? (now && *now == previous) : !now;
        if (!unchanged) {
            if (had_previous) {
                if (PointerTarget* target = live_target(world, previous)) {
                    target->hovered = false;
                    send(world, PointerTargetEvent::Kind::Exit, previous, window, MouseButton::None,
                            cursor_world(world, window, previous, position));
                }
                state.hovered.erase(window);
            }
            if (now) {
                state.hovered[window] = *now;
                if (PointerTarget* target = live_target(world, *now)) {
                    target->hovered = true;
                }
                send(world, PointerTargetEvent::Kind::Enter, *now, window, MouseButton::None,
                        cursor_world(world, window, *now, position));
            }
        }
        if (now) {
            presentation.mouse.consumed_windows.insert(window);
        }
    }

    // A pressed target that was destroyed or disabled releases its press.
    for (auto it = state.pressed.begin(); it != state.pressed.end();) {
        const PointerTarget* target = live_target(world, it->second.entity);
        if (target == nullptr || !target->enabled) {
            if (PointerTarget* gone = live_target(world, it->second.entity)) {
                gone->pressed = false;
            }
            it = state.pressed.erase(it);
        } else {
            ++it;
        }
    }
}

}
