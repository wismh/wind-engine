#include "hierarchy_input.h"

#include "canvas_pointer.h"

#include <engine/ecs/events.h>
#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/tree.h>

#include <expected>
#include <optional>

namespace editor {

namespace {

engine::ui::Element* find_rename_input(engine::ui::Element& element) {
    if (element.kind == engine::ui::ElementKind::TextInput && !element.display_none) {
        return &element;
    }
    for (engine::ui::Element& child : element.children) {
        if (engine::ui::Element* found = find_rename_input(child)) {
            return found;
        }
    }
    for (engine::ui::Element& child : element.generated_items) {
        if (engine::ui::Element* found = find_rename_input(child)) {
            return found;
        }
    }
    return nullptr;
}

}

void HierarchyInput::focus_rename(engine::ecs::World& world, engine::ecs::Entity canvas_entity) {
    if (!hierarchy_->rename_focus_pending()) {
        return;
    }
    engine::ui::UiInstance* instance = world.try_get<engine::ui::UiInstance>(canvas_entity);
    if (instance == nullptr) {
        return;
    }
    engine::ui::Element* input = find_rename_input(instance->document.root);
    if (input == nullptr) {
        return;
    }
    const engine::ui::UiCanvas& canvas = world.get<engine::ui::UiCanvas>(canvas_entity);
    engine::ui::set_focus(world, canvas.window, canvas_entity, input);
    hierarchy_->rename_focused();
}

std::optional<engine::ecs::Entity> HierarchyInput::row_at(
        engine::ecs::World& world, engine::ecs::Entity canvas_entity, glm::vec2 window_point) const {
    engine::ui::UiInstance* instance = world.try_get<engine::ui::UiInstance>(canvas_entity);
    const engine::ui::UiCanvas* canvas = world.try_get<engine::ui::UiCanvas>(canvas_entity);
    if (instance == nullptr || canvas == nullptr) {
        return std::nullopt;
    }
    const engine::ui::UiCanvasSpace space =
            engine::ui::canvas_layout_space(canvas->rect, canvas->fit, canvas->reference_size);
    const float x = (window_point.x - space.offset.x) / space.scale;
    const float y = (window_point.y - space.offset.y) / space.scale;
    engine::ui::Element* hit = engine::ui::hit_test(instance->document.root, x, y);
    while (hit != nullptr) {
        if (hit->generated_owner != nullptr) {
            const std::optional<engine::ecs::Entity> entity =
                    hierarchy_->entity_for(static_cast<const engine::ui::ViewModel*>(hit->generated_owner));
            if (entity.has_value()) {
                return entity;
            }
        }
        if (hit->parent.is_null()) {
            break;
        }
        const std::expected<engine::ui::Element*, engine::ui::ElementLookupError> parent =
                instance->document.find(hit->parent);
        if (!parent) {
            break;
        }
        hit = *parent;
    }
    return std::nullopt;
}

void HierarchyInput::read_drag(engine::ecs::World& world, engine::ecs::Entity canvas_entity) {
    constexpr float kDragSlop = 4.0f;
    for (const engine::MouseEvent& event : engine::ecs::EventReader<engine::MouseEvent>{world, mouse_cursor_}) {
        const engine::ui::UiCanvas* canvas = world.try_get<engine::ui::UiCanvas>(canvas_entity);
        if (canvas == nullptr || event.window != canvas->window) {
            continue;
        }
        if (event.kind == engine::MouseEvent::Kind::Down && event.button == engine::MouseButton::Left) {
            drag_armed_ = false;
            drag_active_ = false;
            drop_target_.reset();
            hierarchy_->set_drop_highlight(std::nullopt);
            if (hierarchy_->renaming() != engine::ecs::Entity{} ||
                    top_canvas_at_pointer(world, event.window) != canvas_entity) {
                continue;
            }
            const std::optional<engine::ecs::Entity> entity = row_at(world, canvas_entity, event.position);
            if (!entity.has_value()) {
                continue;
            }
            drag_armed_ = true;
            drag_entity_ = *entity;
            drag_origin_ = event.position;
            continue;
        }
        if (!drag_armed_) {
            continue;
        }
        if (event.kind == engine::MouseEvent::Kind::Move) {
            const float dx = event.position.x - drag_origin_.x;
            const float dy = event.position.y - drag_origin_.y;
            if (!drag_active_ && dx * dx + dy * dy < kDragSlop * kDragSlop) {
                continue;
            }
            drag_active_ = true;
            const bool over_canvas = top_canvas_at_pointer(world, event.window) == canvas_entity;
            const std::optional<engine::ecs::Entity> hover =
                    over_canvas ? row_at(world, canvas_entity, event.position) : std::nullopt;
            if (over_canvas) {
                if (hover.has_value()) {
                    if (hierarchy_->can_drop(drag_entity_, hover)) {
                        drop_target_ = hover;
                        hierarchy_->set_drop_highlight(hover);
                    } else {
                        drop_target_.reset();
                        hierarchy_->set_drop_highlight(std::nullopt);
                    }
                } else {
                    if (hierarchy_->can_drop(drag_entity_, std::nullopt)) {
                        drop_target_ = std::optional<engine::ecs::Entity>{std::nullopt};
                        hierarchy_->set_drop_highlight(std::nullopt);
                    } else {
                        drop_target_.reset();
                        hierarchy_->set_drop_highlight(std::nullopt);
                    }
                }
            } else {
                drop_target_.reset();
                hierarchy_->set_drop_highlight(std::nullopt);
            }
            continue;
        }
        if (event.kind == engine::MouseEvent::Kind::Up && event.button == engine::MouseButton::Left) {
            if (drag_active_ && drop_target_.has_value()) {
                hierarchy_->reparent(drag_entity_, *drop_target_);
            }
            drag_armed_ = false;
            drag_active_ = false;
            drop_target_.reset();
            hierarchy_->set_drop_highlight(std::nullopt);
        }
    }
}

bool HierarchyInput::handle_key(const engine::KeyEvent& event, bool ctrl, bool shift) {
    if (event.repeat) {
        return false;
    }
    if (event.key == engine::KeyCode::F2 && !ctrl && !shift) {
        hierarchy_->begin_rename();
        return true;
    }
    if (event.key == engine::KeyCode::Delete && !ctrl && !shift) {
        hierarchy_->remove_selected();
        return true;
    }
    if (ctrl && !shift && event.key == engine::KeyCode::N) {
        hierarchy_->create_entity(std::nullopt);
        return true;
    }
    if (event.key == engine::KeyCode::Escape && !ctrl && !shift) {
        if (hierarchy_->renaming() != engine::ecs::Entity{}) {
            hierarchy_->cancel_rename();
            return true;
        }
    }
    return false;
}

}
