#include <engine/core/input_system.h>

#include <engine/ecs/events.h>

#include <algorithm>
#include <cctype>

namespace engine {
namespace {

bool is_whitespace_only(std::string_view name) {
    for (char c : name) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

Control key_control(KeyCode key) {
    return Control{ControlKind::Key, static_cast<std::uint32_t>(key), 0};
}

Control mouse_control(MouseButton button) {
    return Control{ControlKind::MouseButton, static_cast<std::uint32_t>(button), 0};
}

Control touch_control(std::uint32_t finger_id) {
    return Control{ControlKind::Touch, finger_id, 0};
}

}

InputSystem::InputSystem(ecs::World& world) : world_(&world) {}

void InputSystem::set_world(ecs::World& world) {
    world_ = &world;
}

ActionId InputSystem::intern(std::string_view name) {
    if (is_whitespace_only(name)) {
        return ActionId::Invalid;
    }
    if (const auto found = find(name)) {
        return *found;
    }
    interned_names_.emplace_back(name);
    const ActionId id{static_cast<std::uint32_t>(interned_names_.size())};
    name_to_id_.emplace(interned_names_.back(), id);
    return id;
}

std::optional<ActionId> InputSystem::find(std::string_view name) const {
    const auto it = name_to_id_.find(name);
    if (it == name_to_id_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::string_view InputSystem::debug_name(ActionId action) const {
    const auto index = static_cast<std::uint32_t>(action);
    if (index == 0 || index > interned_names_.size()) {
        return {};
    }
    return interned_names_[index - 1];
}

void InputSystem::bind(Control control, ActionId action) {
    if (action == ActionId::Invalid) {
        return;
    }
    if (control.kind == ControlKind::MouseButton &&
        control.code == static_cast<std::uint32_t>(MouseButton::None)) {
        return;
    }
    if (bindings_.contains(control) && down_keys_.contains(control)) {
        unbind(control);
    }
    bindings_[control] = action;
}

void InputSystem::bind(KeyCode key, ActionId action) {
    bind(key_control(key), action);
}

void InputSystem::bind(KeyCode key, std::string_view name) {
    const ActionId action = intern(name);
    if (action == ActionId::Invalid) {
        return;
    }
    bind(key, action);
}

void InputSystem::bind(MouseButton button, ActionId action) {
    if (button == MouseButton::None) {
        return;
    }
    bind(mouse_control(button), action);
}

void InputSystem::bind(MouseButton button, std::string_view name) {
    const ActionId action = intern(name);
    if (action == ActionId::Invalid) {
        return;
    }
    bind(button, action);
}

void InputSystem::unbind(Control control) {
    const auto it = bindings_.find(control);
    if (it == bindings_.end()) {
        return;
    }
    const ActionId previous = it->second;
    release_held(control, previous);
    bindings_.erase(it);
}

void InputSystem::unbind(KeyCode key) {
    unbind(key_control(key));
}

void InputSystem::unbind(MouseButton button) {
    unbind(mouse_control(button));
}

ActionId InputSystem::bound_action(Control control) const {
    const auto it = bindings_.find(control);
    if (it == bindings_.end()) {
        return ActionId::Invalid;
    }
    return it->second;
}

ActionId InputSystem::bound_action(KeyCode key) const {
    return bound_action(key_control(key));
}

ActionId InputSystem::bound_action(MouseButton button) const {
    return bound_action(mouse_control(button));
}

std::vector<Control> InputSystem::controls_for(ActionId action) const {
    std::vector<Control> controls;
    if (action == ActionId::Invalid) {
        return controls;
    }
    for (const auto& [control, bound] : bindings_) {
        if (bound == action) {
            controls.push_back(control);
        }
    }
    std::sort(controls.begin(), controls.end(), [](const Control& a, const Control& b) {
        if (a.kind != b.kind) {
            return a.kind < b.kind;
        }
        if (a.code != b.code) {
            return a.code < b.code;
        }
        return a.device < b.device;
    });
    return controls;
}

void InputSystem::release_held(Control control, ActionId action) {
    if (!down_keys_.erase(control)) {
        return;
    }
    auto held = held_counts_.find(action);
    if (held != held_counts_.end()) {
        --held->second;
        if (held->second <= 0) {
            held_counts_.erase(held);
        }
    }
    if (world_ != nullptr) {
        ecs::EventWriter<InputEvent>{*world_}.send(InputEvent{action, InputEvent::Kind::Up, 0.f});
    }
}

void InputSystem::apply_digital(Control control, bool down) {
    const auto binding = bindings_.find(control);
    if (binding == bindings_.end()) {
        return;
    }

    const ActionId action = binding->second;
    if (down) {
        if (down_keys_.contains(control)) {
            return;
        }
        down_keys_.insert(control);
        ++held_counts_[action];
        ecs::EventWriter<InputEvent>{*world_}.send(InputEvent{action, InputEvent::Kind::Down, 1.f});
        return;
    }

    release_held(control, action);
}

void InputSystem::handle_key(KeyCode key, bool down) {
    if (world_ == nullptr) {
        return;
    }
    apply_digital(key_control(key), down);
}

void InputSystem::handle_mouse_button(MouseButton button, bool down, glm::vec2 position) {
    if (world_ == nullptr) {
        return;
    }
    ecs::EventWriter<MouseEvent>{*world_}.send(MouseEvent{
            .kind = down ? MouseEvent::Kind::Down : MouseEvent::Kind::Up,
            .position = position,
            .button = button,
    });
    if (button == MouseButton::None) {
        return;
    }
    apply_digital(mouse_control(button), down);
}

void InputSystem::handle_mouse_move(glm::vec2 position, glm::vec2 relative) {
    if (world_ == nullptr) {
        return;
    }
    ecs::EventWriter<MouseEvent>{*world_}.send(MouseEvent{
            .kind = MouseEvent::Kind::Move,
            .position = position,
            .relative = relative,
    });
}

void InputSystem::handle_touch(std::uint32_t finger_id, bool down, glm::vec2 position) {
    if (world_ == nullptr) {
        return;
    }
    if (down) {
        if (!primary_finger_.has_value()) {
            primary_finger_ = finger_id;
            handle_mouse_button(MouseButton::Left, true, position);
        }
        apply_digital(touch_control(finger_id), true);
        return;
    }
    apply_digital(touch_control(finger_id), false);
    if (primary_finger_ == finger_id) {
        handle_mouse_button(MouseButton::Left, false, position);
        primary_finger_.reset();
    }
}

void InputSystem::handle_touch_move(std::uint32_t finger_id, glm::vec2 position, glm::vec2 relative) {
    if (world_ == nullptr) {
        return;
    }
    if (primary_finger_ == finger_id) {
        handle_mouse_move(position, relative);
    }
}

bool InputSystem::is_held(ActionId action) const {
    const auto it = held_counts_.find(action);
    return it != held_counts_.end() && it->second > 0;
}

std::optional<std::uint32_t> InputSystem::primary_touch_finger() const {
    return primary_finger_;
}

}
