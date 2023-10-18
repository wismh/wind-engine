#include <engine/core/input_system.h>

#include <engine/ecs/events.h>

namespace engine {

InputSystem::InputSystem(ecs::World& world) : world_(&world) {}

void InputSystem::bind(KeyCode key, std::string action) {
    bindings_[static_cast<std::uint32_t>(key)] = std::move(action);
}

void InputSystem::handle_key(KeyCode key, bool down) {
    const auto code = static_cast<std::uint32_t>(key);
    const auto binding = bindings_.find(code);
    if (binding == bindings_.end()) {
        return;
    }

    const std::string& action = binding->second;
    if (down) {
        if (down_keys_.contains(code)) {
            return;
        }
        down_keys_.insert(code);
        ++held_counts_[action];
        ecs::EventWriter<InputEvent>{*world_}.send(InputEvent{action, InputEvent::Kind::Down});
        return;
    }

    if (!down_keys_.contains(code)) {
        return;
    }
    down_keys_.erase(code);
    auto held = held_counts_.find(action);
    if (held != held_counts_.end()) {
        --held->second;
        if (held->second <= 0) {
            held_counts_.erase(held);
        }
    }
    ecs::EventWriter<InputEvent>{*world_}.send(InputEvent{action, InputEvent::Kind::Up});
}

void InputSystem::handle_mouse_button(MouseButton button, bool down, glm::vec2 position) {
    ecs::EventWriter<MouseEvent>{*world_}.send(MouseEvent{
            .kind = down ? MouseEvent::Kind::Down : MouseEvent::Kind::Up,
            .position = position,
            .button = button,
    });
}

void InputSystem::handle_mouse_move(glm::vec2 position, glm::vec2 relative) {
    ecs::EventWriter<MouseEvent>{*world_}.send(MouseEvent{
            .kind = MouseEvent::Kind::Move,
            .position = position,
            .relative = relative,
    });
}

bool InputSystem::is_held(std::string_view action) const {
    const auto it = held_counts_.find(std::string{action});
    return it != held_counts_.end() && it->second > 0;
}

}
