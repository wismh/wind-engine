#include <engine/core/input_system.h>

#include <engine/ecs/events.h>

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

void InputSystem::handle_key(KeyCode key, bool down) {
    if (world_ == nullptr) {
        return;
    }
    const Control control = key_control(key);
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

    if (!down_keys_.contains(control)) {
        return;
    }
    down_keys_.erase(control);
    auto held = held_counts_.find(action);
    if (held != held_counts_.end()) {
        --held->second;
        if (held->second <= 0) {
            held_counts_.erase(held);
        }
    }
    ecs::EventWriter<InputEvent>{*world_}.send(InputEvent{action, InputEvent::Kind::Up, 0.f});
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

bool InputSystem::is_held(ActionId action) const {
    const auto it = held_counts_.find(action);
    return it != held_counts_.end() && it->second > 0;
}

}
