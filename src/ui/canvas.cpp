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

Element* find_button_at(Element& element, float x, float y) {
    if (!rect_contains(element.layout_rect, x, y)) {
        return nullptr;
    }
    for (auto it = element.children.rbegin(); it != element.children.rend(); ++it) {
        if (Element* nested = find_button_at(*it, x, y)) {
            return nested;
        }
    }
    for (auto it = element.generated_items.rbegin(); it != element.generated_items.rend(); ++it) {
        if (Element* nested = find_button_at(*it, x, y)) {
            return nested;
        }
    }
    if (element.kind == ElementKind::Button) {
        return &element;
    }
    return nullptr;
}

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
    const WindowSize& size = world.ctx<WindowSize>();
    auto view = world.view<UiCanvas>();
    for (ecs::Entity entity : view) {
        UiCanvas& canvas = view.get<UiCanvas>(entity);
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
    world.ctx<MouseConsumed>().value = false;
    apply_canvas_fit(world);
}

void handle_pointer(ecs::World& world, float x, float y) {
    std::vector<CanvasHit> hits;
    {
        auto view = world.view<UiCanvas>();
        for (ecs::Entity entity : view) {
            const UiCanvas& canvas = view.get<UiCanvas>(entity);
            if (!rect_contains(canvas.rect, x, y)) {
                continue;
            }
            hits.push_back(CanvasHit{canvas.order, entity.index, entity});
        }
    }
    if (hits.empty()) {
        return;
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
        return;
    }

    const Stylesheet* sheet = nullptr;
    if (instance->stylesheet) {
        sheet = &*instance->stylesheet;
    }
    if (canvas.data_context) {
        (void) apply_bindings(instance->document, *canvas.data_context, nullptr);
    }
    const WindowSize& size = world.ctx<WindowSize>();
    const UiCanvasSpace space = canvas_layout_space(canvas.rect, canvas.fit, canvas.reference_size);
    const float media_width = space.reference_space ? space.layout_rect.w : static_cast<float>(size.width);
    const float media_height = space.reference_space ? space.layout_rect.h : static_cast<float>(size.height);
    apply_layout_style(instance->document.root, sheet, media_width, media_height);
    layout(instance->document, space.layout_rect);

    Element* button =
            find_button_at(instance->document.root, (x - space.offset.x) / space.scale, (y - space.offset.y) / space.scale);
    if (button == nullptr) {
        return;
    }

    world.ctx<MouseConsumed>().value = true;

    ICommand* command = button->command;
    if (command == nullptr && is_bound(button->command_binding) && canvas.data_context) {
        command = canvas.data_context->find_command(button->command_binding);
    }
    if (command != nullptr && command->can_execute()) {
        command->execute();
    }
}

}
