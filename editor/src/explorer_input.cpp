#include "explorer_input.h"

#include "canvas_pointer.h"

#include <engine/ecs/events.h>
#include <engine/ecs/world.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/tree.h>

#include <expected>
#include <string>
#include <utility>

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

void ExplorerInput::focus_rename(engine::ecs::World& world, engine::ecs::Entity canvas_entity) {
    if (!explorer_->rename_focus_pending()) {
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
    explorer_->rename_focused();
}

std::string ExplorerInput::row_at(engine::ecs::World& world, engine::ecs::Entity canvas_entity,
        glm::vec2 window_point) const {
    engine::ui::UiInstance* instance = world.try_get<engine::ui::UiInstance>(canvas_entity);
    const engine::ui::UiCanvas* canvas = world.try_get<engine::ui::UiCanvas>(canvas_entity);
    if (instance == nullptr || canvas == nullptr) {
        return {};
    }
    const engine::ui::UiCanvasSpace space =
            engine::ui::canvas_layout_space(canvas->rect, canvas->fit, canvas->reference_size);
    const float x = (window_point.x - space.offset.x) / space.scale;
    const float y = (window_point.y - space.offset.y) / space.scale;
    engine::ui::Element* hit = engine::ui::hit_test(instance->document.root, x, y);
    while (hit != nullptr) {
        if (hit->generated_owner != nullptr) {
            const std::string key =
                    explorer_->key_for(static_cast<const engine::ui::ViewModel*>(hit->generated_owner));
            if (!key.empty()) {
                return key;
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
    return {};
}

void ExplorerInput::read_drag(engine::ecs::World& world, engine::ecs::Entity canvas_entity) {
    constexpr float kDragSlop = 4.0f;
    for (const engine::MouseEvent& event : engine::ecs::EventReader<engine::MouseEvent>{world, mouse_cursor_}) {
        const engine::ui::UiCanvas* canvas = world.try_get<engine::ui::UiCanvas>(canvas_entity);
        if (canvas == nullptr || event.window != canvas->window) {
            continue;
        }
        if (event.kind == engine::MouseEvent::Kind::Down && event.button == engine::MouseButton::Left) {
            drag_armed_ = false;
            drag_active_ = false;
            drag_folder_.reset();
            explorer_->set_drop_highlight({});
            if (!explorer_->renaming().empty() ||
                    top_canvas_at_pointer(world, event.window) != canvas_entity) {
                continue;
            }
            const std::string key = row_at(world, canvas_entity, event.position);
            if (key.empty()) {
                continue;
            }
            drag_armed_ = true;
            drag_key_ = key;
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
            const std::string hover = top_canvas_at_pointer(world, event.window) == canvas_entity
                    ? row_at(world, canvas_entity, event.position)
                    : std::string{};
            drag_folder_ = explorer_->drop_folder(drag_key_, hover);
            explorer_->set_drop_highlight(drag_folder_.value_or(""));
            continue;
        }
        if (event.kind == engine::MouseEvent::Kind::Up && event.button == engine::MouseButton::Left) {
            if (drag_active_ && drag_folder_) {
                explorer_->move_into(drag_key_, *drag_folder_);
            }
            drag_armed_ = false;
            drag_active_ = false;
            drag_folder_.reset();
            explorer_->set_drop_highlight({});
        }
    }
}

bool ExplorerInput::handle_key(const engine::KeyEvent& event, bool ctrl, bool shift) {
    if (event.repeat) {
        return false;
    }
    if (event.key == engine::KeyCode::F2 && !ctrl && !shift) {
        explorer_->begin_rename();
        return true;
    }
    if (ctrl && shift && event.key == engine::KeyCode::N) {
        explorer_->new_folder();
        return true;
    }
    if (ctrl && !shift && event.key == engine::KeyCode::C) {
        explorer_->copy();
        return true;
    }
    if (ctrl && !shift && event.key == engine::KeyCode::X) {
        explorer_->cut();
        return true;
    }
    if (ctrl && !shift && event.key == engine::KeyCode::V) {
        explorer_->paste();
        return true;
    }
    if (event.key == engine::KeyCode::Delete && !ctrl) {
        explorer_->remove();
        return true;
    }
    return false;
}

}
