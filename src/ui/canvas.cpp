#include <engine/ui/canvas.h>

#include <engine/ui/document.h>

#include "painter.h"

#include <algorithm>
#include <cmath>
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

struct PreparedCanvas {
    UiCanvas* canvas = nullptr;
    UiInstance* instance = nullptr;
    ecs::Entity entity{};
    UiCanvasSpace space{};
    glm::vec2 layout_pointer{};
};

std::optional<PreparedCanvas> prepare_top_canvas(ecs::World& world, float x, float y, WindowId window) {
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
    layout(instance->document, space.layout_rect, layout_painter_for(world, window));

    const glm::vec2 layout_pointer{(x - space.offset.x) / space.scale, (y - space.offset.y) / space.scale};
    return PreparedCanvas{&canvas, instance, entity, space, layout_pointer};
}

// Shared by handle_pointer() and update_pointer_hover(): finds the topmost element under (x, y),
// rebuilding bindings/layout the same way for both so a hover hit test sees the exact same
// element a click at that position would. Layout uses the window's IUiPainter when registered
// (UiLayoutPainters) so hug text metrics match paint_document; otherwise the CPU fallback.
// Sets MouseConsumed as a side effect whenever it finds a hit (matching the previous
// handle_pointer() behavior) — both callers want that.
std::optional<PointerHit> resolve_pointer_hit(ecs::World& world, float x, float y, WindowId window) {
    const std::optional<PreparedCanvas> prepared = prepare_top_canvas(world, x, y, window);
    if (!prepared) {
        return std::nullopt;
    }

    Element* hit = hit_test(prepared->instance->document.root, prepared->layout_pointer.x, prepared->layout_pointer.y);
    if (hit == nullptr) {
        return std::nullopt;
    }

    world.ctx<MouseConsumed>().consumed_windows.insert(window);
    return PointerHit{hit, prepared->canvas, prepared->entity, prepared->space};
}

// Shared axis math for handle_pointer()'s drag-start and update_drag()'s continuation: maps a
// window-space (x, y) into the drag's layout-space rect (the same `(v - offset) / scale` transform
// resolve_pointer_hit() applies before hit-testing) and returns the clamped [0,1] fraction along
// `orientation`'s axis of `rect`. No min/max/step — remapping a raw fraction into a domain-specific
    // range belongs to the game's ViewModel, not the engine.
float compute_drag_fraction(StackDirection orientation, const render::Rect& rect, glm::vec2 space_offset,
        float space_scale, float x, float y) {
    const float local_x = (x - space_offset.x) / space_scale;
    const float local_y = (y - space_offset.y) / space_scale;
    const float t = orientation == StackDirection::Horizontal
            ? (rect.w > 0.0f ? (local_x - rect.x) / rect.w : 0.0f)
            : (rect.h > 0.0f ? (local_y - rect.y) / rect.h : 0.0f);
    return std::clamp(t, 0.0f, 1.0f);
}

bool build_element_path(const Element& current, const Element* target, std::vector<std::size_t>& path) {
    if (&current == target) {
        return true;
    }
    for (std::size_t i = 0; i < current.children.size(); ++i) {
        path.push_back(i);
        if (build_element_path(current.children[i], target, path)) {
            return true;
        }
        path.pop_back();
    }
    for (std::size_t i = 0; i < current.generated_items.size(); ++i) {
        path.push_back(i | 0x80000000ULL);
        if (build_element_path(current.generated_items[i], target, path)) {
            return true;
        }
        path.pop_back();
    }
    return false;
}

std::vector<std::size_t> find_element_path(const Element& root, const Element* target) {
    std::vector<std::size_t> path;
    build_element_path(root, target, path);
    return path;
}

Element* resolve_element_path(Element& root, const std::vector<std::size_t>& path) {
    Element* curr = &root;
    for (std::size_t step : path) {
        if ((step & 0x80000000ULL) != 0) {
            const std::size_t idx = step & ~0x80000000ULL;
            if (idx < curr->generated_items.size()) {
                curr = &curr->generated_items[idx];
            } else {
                return nullptr;
            }
        } else {
            if (step < curr->children.size()) {
                curr = &curr->children[step];
            } else {
                return nullptr;
            }
        }
    }
    return curr;
}

}

void handle_pointer(ecs::World& world, float x, float y, WindowId window) {
    const std::optional<PointerHit> hit = resolve_pointer_hit(world, x, y, window);
    if (!hit) {
        clear_focus(world, window);
        return;
    }

    if (hit->element->kind == ElementKind::TextInput) {
        set_focus(world, window, hit->entity, hit->element);
    } else {
        clear_focus(world, window);
    }

    if (hit->element->kind == ElementKind::Viewport && has_viewport_camera(*hit->element) && hit->canvas->data_context) {
        world.ctx<UiActivePans>().pans[window] = ActivePan{
                hit->entity,
                hit->element->pan_x_binding,
                hit->element->pan_y_binding,
                hit->element->zoom_binding,
                glm::vec2{x, y},
                hit->space.offset,
                hit->space.scale,
                hit->element->generated_owner,
        };
    } else if (is_bound(hit->element->drag_binding) && hit->canvas->data_context) {
        // A drag-bound element generated inside an ItemsControl/ItemTemplate has its `drag`
        // binding registered on the *item* ViewModel (Element::generated_owner, freshly resolved
        // by the apply_bindings() resolve_pointer_hit() just ran), not the canvas's own
        // data_context — writing to data_context there would silently no-op forever.
        ViewModel* target = hit->element->generated_owner != nullptr
                ? static_cast<ViewModel*>(const_cast<void*>(hit->element->generated_owner))
                : hit->canvas->data_context.get();
        const float fraction = compute_drag_fraction(hit->element->drag_orientation, hit->element->layout_rect,
                hit->space.offset, hit->space.scale, x, y);
        target->write_property_float(hit->element->drag_binding, fraction);
        world.ctx<UiActiveDrags>().drags[window] = ActiveDrag{
                hit->entity,
                hit->element->drag_binding,
                hit->element->layout_rect,
                hit->space.offset,
                hit->space.scale,
                hit->element->drag_orientation,
                hit->element->generated_owner,
        };
    }

    bool clicked_scrollbar = false;
    const glm::vec2 local_pointer{
            (x - hit->space.offset.x) / hit->space.scale, (y - hit->space.offset.y) / hit->space.scale};
    if (is_scrollable_y(*hit->element)) {
        const render::Rect track = scrollbar_track_rect(*hit->element);
        const render::Rect thumb = scrollbar_thumb_rect(*hit->element);
        if (track.w > 0.0f && track.h > 0.0f && rect_contains(track, local_pointer.x, local_pointer.y)) {
            clicked_scrollbar = true;
            UiInstance& inst = world.get<UiInstance>(hit->entity);
            if (rect_contains(thumb, local_pointer.x, local_pointer.y)) {
                hit->element->scrollbar_dragging = true;
                world.ctx<UiActiveScrollbars>().drags[window] = ActiveScrollbarDrag{
                        hit->entity,
                        find_element_path(inst.document.root, hit->element),
                        hit->element->generated_owner,
                        local_pointer.y,
                        hit->element->scroll_y,
                        track.h,
                        thumb.h,
                        hit->element->max_scroll_y,
                        hit->space.offset,
                        hit->space.scale,
                        hit->element->scroll_y_binding,
                };
            } else {
                const float available = track.h - thumb.h;
                if (available > 0.0f) {
                    const float target_thumb_y = local_pointer.y - track.y - thumb.h * 0.5f;
                    const float fraction = std::clamp(target_thumb_y / available, 0.0f, 1.0f);
                    hit->element->scroll_y = fraction * hit->element->max_scroll_y;
                    if (is_bound(hit->element->scroll_y_binding) && hit->canvas->data_context) {
                        ViewModel* target = hit->element->generated_owner != nullptr
                                ? static_cast<ViewModel*>(const_cast<void*>(hit->element->generated_owner))
                                : hit->canvas->data_context.get();
                        target->write_property_float(hit->element->scroll_y_binding, hit->element->scroll_y);
                    }
                }
            }
        }
    }

    if (!clicked_scrollbar && hit->element->kind != ElementKind::TextInput) {
        ICommand* command = hit->element->command;
        if (command == nullptr && is_bound(hit->element->command_binding) && hit->canvas->data_context) {
            command = hit->canvas->data_context->find_command(hit->element->command_binding);
        }
        if (command != nullptr && command->can_execute()) {
            command->execute();
        }
    }
}

void update_drag(ecs::World& world, float x, float y, WindowId window) {
    auto& scroll_drags = world.ctx<UiActiveScrollbars>().drags;
    if (const auto sit = scroll_drags.find(window); sit != scroll_drags.end()) {
        const ActiveScrollbarDrag& sdrag = sit->second;
        UiInstance* instance = world.try_get<UiInstance>(sdrag.canvas_entity);
        if (instance != nullptr) {
            Element* elem = resolve_element_path(instance->document.root, sdrag.path);
            if (elem != nullptr) {
                const float local_y = (y - sdrag.space_offset.y) / sdrag.space_scale;
                const float dy = local_y - sdrag.drag_start_pointer_y;
                const float available = sdrag.track_h - sdrag.thumb_h;
                if (available > 0.0f) {
                    const float delta_scroll = (dy / available) * sdrag.max_scroll_y;
                    elem->scroll_y = std::clamp(sdrag.drag_start_scroll_y + delta_scroll, 0.0f, sdrag.max_scroll_y);
                    if (is_bound(sdrag.scroll_y_binding)) {
                        UiCanvas* canvas = world.try_get<UiCanvas>(sdrag.canvas_entity);
                        if (canvas != nullptr && canvas->data_context) {
                            ViewModel* target = sdrag.owner != nullptr
                                    ? static_cast<ViewModel*>(const_cast<void*>(sdrag.owner))
                                    : canvas->data_context.get();
                            target->write_property_float(sdrag.scroll_y_binding, elem->scroll_y);
                        }
                    }
                }
            }
        }
    }

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

    ViewModel* target = canvas->data_context.get();
    if (drag.owner != nullptr) {
        UiInstance* instance = world.try_get<UiInstance>(drag.canvas_entity);
        if (instance == nullptr) {
            drags.erase(it);
            return;
        }
        (void) apply_bindings(instance->document, *canvas->data_context, nullptr);
        const Element* owner_element = find_by_generated_owner(instance->document.root, drag.owner);
        if (owner_element == nullptr) {
            drags.erase(it);
            return;
        }
        target = static_cast<ViewModel*>(const_cast<void*>(drag.owner));
    }

    const float fraction =
            compute_drag_fraction(drag.orientation, drag.rect, drag.space_offset, drag.space_scale, x, y);
    target->write_property_float(drag.value_binding, fraction);
}

void end_drag(ecs::World& world, WindowId window) {
    auto& scroll_drags = world.ctx<UiActiveScrollbars>().drags;
    if (const auto sit = scroll_drags.find(window); sit != scroll_drags.end()) {
        UiInstance* instance = world.try_get<UiInstance>(sit->second.canvas_entity);
        if (instance != nullptr) {
            Element* elem = resolve_element_path(instance->document.root, sit->second.path);
            if (elem != nullptr) {
                elem->scrollbar_dragging = false;
            }
        }
        scroll_drags.erase(sit);
    }
    world.ctx<UiActiveDrags>().drags.erase(window);
}

ViewModel* pan_target(ecs::World& world, const ActivePan& pan, UiCanvas& canvas) {
    if (pan.owner == nullptr) {
        return canvas.data_context.get();
    }
    UiInstance* instance = world.try_get<UiInstance>(pan.canvas_entity);
    if (instance == nullptr) {
        return nullptr;
    }
    (void) apply_bindings(instance->document, *canvas.data_context, nullptr);
    if (find_by_generated_owner(instance->document.root, pan.owner) == nullptr) {
        return nullptr;
    }
    return static_cast<ViewModel*>(const_cast<void*>(pan.owner));
}

void update_pan(ecs::World& world, float x, float y, WindowId window) {
    auto& pans = world.ctx<UiActivePans>().pans;
    const auto it = pans.find(window);
    if (it == pans.end()) {
        return;
    }
    ActivePan& pan = it->second;
    UiCanvas* canvas = world.try_get<UiCanvas>(pan.canvas_entity);
    if (canvas == nullptr || !canvas->data_context) {
        pans.erase(it);
        return;
    }
    ViewModel* target = pan_target(world, pan, *canvas);
    if (target == nullptr) {
        pans.erase(it);
        return;
    }

    float zoom = 1.0f;
    if (is_bound(pan.zoom_binding)) {
        zoom = viewport_zoom(target->read_property_float(pan.zoom_binding).value_or(1.0f));
    }
    const float scale = pan.space_scale * zoom;
    const glm::vec2 delta{(x - pan.last_pointer.x) / scale, (y - pan.last_pointer.y) / scale};
    pan.last_pointer = glm::vec2{x, y};
    if (is_bound(pan.pan_x_binding)) {
        const float current = target->read_property_float(pan.pan_x_binding).value_or(0.0f);
        target->write_property_float(pan.pan_x_binding, current + delta.x);
    }
    if (is_bound(pan.pan_y_binding)) {
        const float current = target->read_property_float(pan.pan_y_binding).value_or(0.0f);
        target->write_property_float(pan.pan_y_binding, current + delta.y);
    }
}

void end_pan(ecs::World& world, WindowId window) {
    world.ctx<UiActivePans>().pans.erase(window);
}

void handle_wheel(ecs::World& world, float x, float y, float wheel_y, WindowId window) {
    if (wheel_y == 0.0f) {
        return;
    }
    const std::optional<PreparedCanvas> prepared = prepare_top_canvas(world, x, y, window);
    if (!prepared) {
        return;
    }

    Element* scrollable = find_scrollable_at(
            prepared->instance->document.root, prepared->layout_pointer.x, prepared->layout_pointer.y);
    if (scrollable != nullptr) {
        constexpr float kScrollStep = 40.0f;
        bool scrolled = false;
        if (is_scrollable_y(*scrollable)) {
            scrollable->scroll_y =
                    std::clamp(scrollable->scroll_y - wheel_y * kScrollStep, 0.0f, scrollable->max_scroll_y);
            if (is_bound(scrollable->scroll_y_binding) && prepared->canvas->data_context) {
                ViewModel* target = scrollable->generated_owner != nullptr
                        ? static_cast<ViewModel*>(const_cast<void*>(scrollable->generated_owner))
                        : prepared->canvas->data_context.get();
                target->write_property_float(scrollable->scroll_y_binding, scrollable->scroll_y);
            }
            scrolled = true;
        } else if (is_scrollable_x(*scrollable)) {
            scrollable->scroll_x =
                    std::clamp(scrollable->scroll_x - wheel_y * kScrollStep, 0.0f, scrollable->max_scroll_x);
            if (is_bound(scrollable->scroll_x_binding) && prepared->canvas->data_context) {
                ViewModel* target = scrollable->generated_owner != nullptr
                        ? static_cast<ViewModel*>(const_cast<void*>(scrollable->generated_owner))
                        : prepared->canvas->data_context.get();
                target->write_property_float(scrollable->scroll_x_binding, scrollable->scroll_x);
            }
            scrolled = true;
        }
        if (scrolled) {
            world.ctx<MouseConsumed>().consumed_windows.insert(window);
            return;
        }
    }

    if (!prepared->canvas->data_context) {
        return;
    }
    Element* viewport = find_viewport_at(
            prepared->instance->document.root, prepared->layout_pointer.x, prepared->layout_pointer.y);
    if (viewport == nullptr || !is_bound(viewport->zoom_binding)) {
        return;
    }

    ViewModel* target = viewport->generated_owner != nullptr
            ? static_cast<ViewModel*>(const_cast<void*>(viewport->generated_owner))
            : prepared->canvas->data_context.get();
    const float z = viewport_zoom(target->read_property_float(viewport->zoom_binding).value_or(viewport->zoom));
    const float new_z = std::clamp(z * std::pow(kViewportZoomStep, wheel_y), kViewportMinZoom, kViewportMaxZoom);
    const glm::vec2 origin{viewport->layout_rect.x, viewport->layout_rect.y};
    glm::vec2 pan{viewport->pan_x, viewport->pan_y};
    if (is_bound(viewport->pan_x_binding)) {
        pan.x = target->read_property_float(viewport->pan_x_binding).value_or(pan.x);
    }
    if (is_bound(viewport->pan_y_binding)) {
        pan.y = target->read_property_float(viewport->pan_y_binding).value_or(pan.y);
    }
    const glm::vec2 new_pan = viewport_pan_after_zoom(origin, pan, z, new_z, prepared->layout_pointer);
    target->write_property_float(viewport->zoom_binding, new_z);
    if (is_bound(viewport->pan_x_binding)) {
        target->write_property_float(viewport->pan_x_binding, new_pan.x);
    }
    if (is_bound(viewport->pan_y_binding)) {
        target->write_property_float(viewport->pan_y_binding, new_pan.y);
    }
    world.ctx<MouseConsumed>().consumed_windows.insert(window);
}

void update_pointer_hover(ecs::World& world, float x, float y, WindowId window) {
    const std::optional<PointerHit> hit = resolve_pointer_hit(world, x, y, window);
    if (hit && is_scrollable_y(*hit->element)) {
        const glm::vec2 local_pointer{
                (x - hit->space.offset.x) / hit->space.scale, (y - hit->space.offset.y) / hit->space.scale};
        const render::Rect thumb = scrollbar_thumb_rect(*hit->element);
        hit->element->scrollbar_thumb_hovered = rect_contains(thumb, local_pointer.x, local_pointer.y);
    }
}

ecs::Entity spawn_canvas(ecs::World& world, UiCanvas canvas, UiDocument document, std::optional<Stylesheet> stylesheet) {
    canvas.document.reset();
    const ecs::Entity entity = world.create();
    world.emplace<UiCanvas>(entity, std::move(canvas));
    world.emplace<UiInstance>(entity, UiInstance{std::move(document), std::move(stylesheet)});
    return entity;
}

namespace {

bool is_utf8_continuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

std::size_t prev_utf8_char(std::string_view s, std::size_t pos) {
    if (pos == 0) {
        return 0;
    }
    --pos;
    while (pos > 0 && is_utf8_continuation(s[pos])) {
        --pos;
    }
    return pos;
}

std::size_t next_utf8_char(std::string_view s, std::size_t pos) {
    if (pos >= s.size()) {
        return s.size();
    }
    ++pos;
    while (pos < s.size() && is_utf8_continuation(s[pos])) {
        ++pos;
    }
    return pos;
}

}

void clear_focus(ecs::World& world, WindowId window) {
    auto& focus_map = world.ctx<UiFocusState>().focused;
    const auto it = focus_map.find(window);
    if (it != focus_map.end()) {
        if (it->second.element != nullptr) {
            it->second.element->focused = false;
            it->second.element->caret_blink_timer = 0.0f;
        }
        focus_map.erase(it);
    }
}

void set_focus(ecs::World& world, WindowId window, ecs::Entity canvas_entity, Element* element) {
    auto& focus_map = world.ctx<UiFocusState>().focused;
    const auto it = focus_map.find(window);
    if (it != focus_map.end() && it->second.element != element) {
        if (it->second.element != nullptr) {
            it->second.element->focused = false;
            it->second.element->caret_blink_timer = 0.0f;
        }
    }
    if (element != nullptr) {
        element->focused = true;
        element->caret_blink_timer = 0.0f;
        if (element->caret_position > element->text.size()) {
            element->caret_position = element->text.size();
        }
        focus_map[window] = UiFocus{canvas_entity, element};
    } else {
        focus_map.erase(window);
    }
}

Element* focused_element(ecs::World& world, WindowId window) {
    auto& focus_map = world.ctx<UiFocusState>().focused;
    const auto it = focus_map.find(window);
    if (it == focus_map.end()) {
        return nullptr;
    }
    return it->second.element;
}

void handle_text_input(ecs::World& world, std::string_view text, WindowId window) {
    if (text.empty()) {
        return;
    }
    auto& focus_map = world.ctx<UiFocusState>().focused;
    const auto it = focus_map.find(window);
    if (it == focus_map.end() || it->second.element == nullptr) {
        return;
    }
    Element* element = it->second.element;
    if (element->disabled) {
        return;
    }

    if (element->caret_position > element->text.size()) {
        element->caret_position = element->text.size();
    }
    element->text.insert(element->caret_position, text);
    element->caret_position += text.size();
    element->caret_blink_timer = 0.0f;

    if (is_bound(element->text_binding)) {
        UiCanvas* canvas = world.try_get<UiCanvas>(it->second.canvas_entity);
        if (canvas != nullptr && canvas->data_context) {
            ViewModel* target = element->generated_owner != nullptr
                    ? static_cast<ViewModel*>(const_cast<void*>(element->generated_owner))
                    : canvas->data_context.get();
            if (target != nullptr) {
                target->write_property_string(element->text_binding, element->text);
            }
        }
    }
}

void handle_key(ecs::World& world, KeyCode key, bool down, bool /*repeat*/, WindowId window) {
    if (!down) {
        return;
    }
    auto& focus_map = world.ctx<UiFocusState>().focused;
    const auto it = focus_map.find(window);
    if (it == focus_map.end() || it->second.element == nullptr) {
        return;
    }
    Element* element = it->second.element;
    if (element->disabled) {
        return;
    }

    if (element->caret_position > element->text.size()) {
        element->caret_position = element->text.size();
    }

    if (key == KeyCode::Escape) {
        clear_focus(world, window);
        return;
    }

    if (key == KeyCode::Return) {
        UiCanvas* canvas = world.try_get<UiCanvas>(it->second.canvas_entity);
        ViewModel* target = nullptr;
        if (canvas != nullptr && canvas->data_context) {
            target = element->generated_owner != nullptr
                    ? static_cast<ViewModel*>(const_cast<void*>(element->generated_owner))
                    : canvas->data_context.get();
        }
        ICommand* command = element->command;
        if (command == nullptr && is_bound(element->command_binding) && target != nullptr) {
            command = target->find_command(element->command_binding);
        }
        if (command != nullptr && command->can_execute()) {
            command->execute();
        }
        return;
    }

    bool text_changed = false;
    if (key == KeyCode::Backspace) {
        if (element->caret_position > 0) {
            const std::size_t prev = prev_utf8_char(element->text, element->caret_position);
            element->text.erase(prev, element->caret_position - prev);
            element->caret_position = prev;
            element->caret_blink_timer = 0.0f;
            text_changed = true;
        }
    } else if (key == KeyCode::Delete) {
        if (element->caret_position < element->text.size()) {
            const std::size_t next = next_utf8_char(element->text, element->caret_position);
            element->text.erase(element->caret_position, next - element->caret_position);
            element->caret_blink_timer = 0.0f;
            text_changed = true;
        }
    } else if (key == KeyCode::Left) {
        element->caret_position = prev_utf8_char(element->text, element->caret_position);
        element->caret_blink_timer = 0.0f;
    } else if (key == KeyCode::Right) {
        element->caret_position = next_utf8_char(element->text, element->caret_position);
        element->caret_blink_timer = 0.0f;
    } else if (key == KeyCode::Home) {
        element->caret_position = 0;
        element->caret_blink_timer = 0.0f;
    } else if (key == KeyCode::End) {
        element->caret_position = element->text.size();
        element->caret_blink_timer = 0.0f;
    }

    if (text_changed && is_bound(element->text_binding)) {
        UiCanvas* canvas = world.try_get<UiCanvas>(it->second.canvas_entity);
        if (canvas != nullptr && canvas->data_context) {
            ViewModel* target = element->generated_owner != nullptr
                    ? static_cast<ViewModel*>(const_cast<void*>(element->generated_owner))
                    : canvas->data_context.get();
            if (target != nullptr) {
                target->write_property_string(element->text_binding, element->text);
            }
        }
    }
}

}
