#include <engine/ui/canvas.h>

#include <engine/ui/document.h>

#include "painter.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace engine::ui {
namespace {

struct CanvasHit {
    int order = 0;
    std::uint32_t index = 0;
    ecs::Entity entity{};
};

render::Rect scaled_fit_rect(glm::vec2 reference_size, float window_width, float window_height) {
    if (reference_size.x <= 0.0f || reference_size.y <= 0.0f) {
        return render::Rect{0.0f, 0.0f, window_width, window_height};
    }
    const float scale = std::min(window_width / reference_size.x, window_height / reference_size.y);
    const float scaled_w = reference_size.x * scale;
    const float scaled_h = reference_size.y * scale;
    return render::Rect{
            (window_width - scaled_w) * 0.5f,
            (window_height - scaled_h) * 0.5f,
            scaled_w,
            scaled_h,
    };
}

}

WindowSize window_size_for(ecs::World& world, WindowId id) {
    const WindowSizes& sizes = world.ctx<WindowSizes>();
    const auto it = sizes.sizes.find(id);
    return it == sizes.sizes.end() ? WindowSize{} : it->second;
}

UiPointer& pointer_for(ecs::World& world, WindowId id) {
    if (id == kPrimaryWindow) {
        return world.ctx<UiPointer>();
    }
    return world.ctx<UiPointers>().pointers[id];
}

UiCanvasSpace canvas_layout_space(const render::Rect& rect, UiFit fit, glm::vec2 reference_size) {
    if (fit == UiFit::ScaleWithScreenSize && reference_size.x > 0.0f && reference_size.y > 0.0f) {
        return UiCanvasSpace{
                render::Rect{0.0f, 0.0f, reference_size.x, reference_size.y},
                glm::vec2{rect.x, rect.y},
                rect.w / reference_size.x,
                true,
        };
    }
    return UiCanvasSpace{rect, glm::vec2{0.0f, 0.0f}, 1.0f, false};
}

void apply_canvas_fit(ecs::World& world) {
    auto view = world.view<UiCanvas>();
    for (ecs::Entity entity : view) {
        UiCanvas& canvas = view.get<UiCanvas>(entity);
        const WindowSize size = window_size_for(world, canvas.window);
        if (canvas.fit == UiFit::FillWindow) {
            canvas.rect = render::Rect{
                    0.0f,
                    0.0f,
                    static_cast<float>(size.width),
                    static_cast<float>(size.height),
            };
        } else if (canvas.fit == UiFit::ScaleWithScreenSize) {
            canvas.rect = scaled_fit_rect(
                    canvas.reference_size, static_cast<float>(size.width), static_cast<float>(size.height));
        }
    }
}

void begin_frame(ecs::World& world) {
    world.ctx<MouseConsumed>().consumed_windows.clear();
    apply_canvas_fit(world);
}

namespace {

// Shared result of resolve_pointer_hit(): everything a caller needs to both resolve this hit
// (command/drag lookup) and, for handle_pointer()'s drag case, capture enough geometry to keep
// tracking the drag on later Move events without re-hit-testing.
struct PointerHit {
    Element* element = nullptr;
    UiCanvas* canvas = nullptr;
    ecs::Entity entity{};
    UiCanvasSpace space{};
};

// Shared by handle_pointer() and update_pointer_hover(): finds the topmost element under (x, y),
// rebuilding bindings/layout the same way for both so a hover hit test sees the exact same
// element a click at that position would. Sets MouseConsumed as a side effect whenever it finds a
// hit (matching the previous handle_pointer() behavior) — both callers want that.
std::optional<PointerHit> resolve_pointer_hit(ecs::World& world, float x, float y, WindowId window) {
    std::vector<CanvasHit> hits;
    {
        auto view = world.view<UiCanvas>();
        for (ecs::Entity entity : view) {
            const UiCanvas& canvas = view.get<UiCanvas>(entity);
            if (canvas.window != window) {
                continue;
            }
            if (!rect_contains(canvas.rect, x, y)) {
                continue;
            }
            hits.push_back(CanvasHit{canvas.order, entity.index, entity});
        }
    }
    if (hits.empty()) {
        return std::nullopt;
    }

    std::stable_sort(hits.begin(), hits.end(), [](const CanvasHit& a, const CanvasHit& b) {
        if (a.order != b.order) {
            return a.order > b.order;
        }
        return a.index > b.index;
    });

    const ecs::Entity entity = hits.front().entity;
    UiCanvas& canvas = world.get<UiCanvas>(entity);
    UiInstance* instance = world.try_get<UiInstance>(entity);
    if (instance == nullptr) {
        return std::nullopt;
    }

    const Stylesheet* sheet = nullptr;
    if (instance->stylesheet) {
        sheet = &*instance->stylesheet;
    }
    if (canvas.data_context) {
        (void) apply_bindings(instance->document, *canvas.data_context, nullptr);
    }
    const WindowSize size = window_size_for(world, canvas.window);
    const UiCanvasSpace space = canvas_layout_space(canvas.rect, canvas.fit, canvas.reference_size);
    const float media_width = space.reference_space ? space.layout_rect.w : static_cast<float>(size.width);
    const float media_height = space.reference_space ? space.layout_rect.h : static_cast<float>(size.height);
    apply_layout_style(instance->document.root, sheet, media_width, media_height);
    layout(instance->document, space.layout_rect);

    Element* hit =
            hit_test(instance->document.root, (x - space.offset.x) / space.scale, (y - space.offset.y) / space.scale);
    if (hit == nullptr) {
        return std::nullopt;
    }

    world.ctx<MouseConsumed>().consumed_windows.insert(window);
    return PointerHit{hit, &canvas, entity, space};
}

// Shared axis math for handle_pointer()'s drag-start and update_drag()'s continuation: maps a
// window-space (x, y) into the drag's layout-space rect (the same `(v - offset) / scale` transform
// resolve_pointer_hit() applies before hit-testing) and returns the clamped [0,1] fraction along
// `orientation`'s axis of `rect`. No min/max/step — remapping a raw fraction into a domain-specific
// range belongs to the game's ViewModel, not the engine (see docs/sdd.md).
float compute_drag_fraction(StackDirection orientation, const render::Rect& rect, glm::vec2 space_offset,
        float space_scale, float x, float y) {
    const float local_x = (x - space_offset.x) / space_scale;
    const float local_y = (y - space_offset.y) / space_scale;
    const float t = orientation == StackDirection::Horizontal
            ? (rect.w > 0.0f ? (local_x - rect.x) / rect.w : 0.0f)
            : (rect.h > 0.0f ? (local_y - rect.y) / rect.h : 0.0f);
    return std::clamp(t, 0.0f, 1.0f);
}

}

void handle_pointer(ecs::World& world, float x, float y, WindowId window) {
    const std::optional<PointerHit> hit = resolve_pointer_hit(world, x, y, window);
    if (!hit) {
        return;
    }

    if (is_bound(hit->element->drag_binding) && hit->canvas->data_context) {
        const float fraction = compute_drag_fraction(hit->element->drag_orientation, hit->element->layout_rect,
                hit->space.offset, hit->space.scale, x, y);
        hit->canvas->data_context->write_property_float(hit->element->drag_binding, fraction);
        world.ctx<UiActiveDrags>().drags[window] = ActiveDrag{
                hit->entity,
                hit->element->drag_binding,
                hit->element->layout_rect,
                hit->space.offset,
                hit->space.scale,
                hit->element->drag_orientation,
        };
    }

    ICommand* command = hit->element->command;
    if (command == nullptr && is_bound(hit->element->command_binding) && hit->canvas->data_context) {
        command = hit->canvas->data_context->find_command(hit->element->command_binding);
    }
    if (command != nullptr && command->can_execute()) {
        command->execute();
    }
}

void update_drag(ecs::World& world, float x, float y, WindowId window) {
    auto& drags = world.ctx<UiActiveDrags>().drags;
    const auto it = drags.find(window);
    if (it == drags.end()) {
        return;
    }
    const ActiveDrag& drag = it->second;
    UiCanvas* canvas = world.try_get<UiCanvas>(drag.canvas_entity);
    if (canvas == nullptr || !canvas->data_context) {
        drags.erase(it);
        return;
    }
    const float fraction =
            compute_drag_fraction(drag.orientation, drag.rect, drag.space_offset, drag.space_scale, x, y);
    canvas->data_context->write_property_float(drag.value_binding, fraction);
}

void end_drag(ecs::World& world, WindowId window) {
    world.ctx<UiActiveDrags>().drags.erase(window);
}

void update_pointer_hover(ecs::World& world, float x, float y, WindowId window) {
    (void) resolve_pointer_hit(world, x, y, window);
}

}
