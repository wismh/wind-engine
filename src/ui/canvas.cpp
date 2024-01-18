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

}

void apply_fill_window(ecs::World& world) {
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
        }
    }
}

void begin_frame(ecs::World& world) {
    world.ctx<MouseConsumed>().value = false;
    apply_fill_window(world);
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
    apply_layout_style(instance->document.root, sheet, static_cast<float>(size.width), static_cast<float>(size.height));
    layout(instance->document, canvas.rect);

    Element* button = find_button_at(instance->document.root, x, y);
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
