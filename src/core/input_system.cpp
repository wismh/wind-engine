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

Control axis_control(MouseAxis axis) {
    return Control{ControlKind::MouseMotion, static_cast<std::uint32_t>(axis), 0};
}

constexpr std::uint32_t kMouseAxisCount = 3;

}

void InputSystem::set_router(std::function<ecs::World*(WindowId)> router) {
    router_ = std::move(router);
}

void InputSystem::reset() {
    primary_finger_.reset();
    bindings_.clear();
    down_keys_.clear();
    held_counts_.clear();
    name_to_id_.clear();
    interned_names_.clear();
    locked_.clear();
    begin_frame();
}

void InputSystem::begin_frame() {
    for (float& motion : mouse_motion_) {
        motion = 0.f;
    }
}

void InputSystem::set_mouse_locked(WindowId window, bool locked) {
    if (locked) {
        locked_.insert(window);
    } else {
        locked_.erase(window);
    }
}

bool InputSystem::is_mouse_locked(WindowId window) const {
    return locked_.contains(window);
}

ecs::World* InputSystem::world_for(WindowId window) const {
    return router_ ? router_(window) : nullptr;
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

void InputSystem::bind(Control control, ActionId action, float scale) {
    if (action == ActionId::Invalid) {
        return;
    }
    if (control.kind == ControlKind::MouseButton &&
        control.code == static_cast<std::uint32_t>(MouseButton::None)) {
        return;
    }
    if (control.kind == ControlKind::MouseMotion && control.code >= kMouseAxisCount) {
        return;
    }
    if (bindings_.contains(control) && down_keys_.contains(control)) {
        unbind(control);
    }
    bindings_[control] = Binding{action, scale};
}

void InputSystem::bind(KeyCode key, ActionId action, float scale) {
    bind(key_control(key), action, scale);
}

void InputSystem::bind(KeyCode key, std::string_view name, float scale) {
    const ActionId action = intern(name);
    if (action == ActionId::Invalid) {
        return;
    }
    bind(key, action, scale);
}

void InputSystem::bind(MouseButton button, ActionId action, float scale) {
    if (button == MouseButton::None) {
        return;
    }
    bind(mouse_control(button), action, scale);
}

void InputSystem::bind(MouseButton button, std::string_view name, float scale) {
    const ActionId action = intern(name);
    if (action == ActionId::Invalid) {
        return;
    }
    bind(button, action, scale);
}

void InputSystem::bind(MouseAxis axis, ActionId action, float scale) {
    bind(axis_control(axis), action, scale);
}

void InputSystem::bind(MouseAxis axis, std::string_view name, float scale) {
    const ActionId action = intern(name);
    if (action == ActionId::Invalid) {
        return;
    }
    bind(axis, action, scale);
}

void InputSystem::unbind(Control control) {
    const auto it = bindings_.find(control);
    if (it == bindings_.end()) {
        return;
    }
    const ActionId previous = it->second.action;
    release_held(control, previous);
    bindings_.erase(it);
}

void InputSystem::unbind(KeyCode key) {
    unbind(key_control(key));
}

void InputSystem::unbind(MouseButton button) {
    unbind(mouse_control(button));
}

void InputSystem::unbind(MouseAxis axis) {
    unbind(axis_control(axis));
}

ActionId InputSystem::bound_action(Control control) const {
    const auto it = bindings_.find(control);
    if (it == bindings_.end()) {
        return ActionId::Invalid;
    }
    return it->second.action;
}

ActionId InputSystem::bound_action(KeyCode key) const {
    return bound_action(key_control(key));
}

ActionId InputSystem::bound_action(MouseButton button) const {
    return bound_action(mouse_control(button));
}

ActionId InputSystem::bound_action(MouseAxis axis) const {
    return bound_action(axis_control(axis));
}

float InputSystem::bound_scale(Control control) const {
    const auto it = bindings_.find(control);
    if (it == bindings_.end()) {
        return 0.f;
    }
    return it->second.scale;
}

std::vector<Control> InputSystem::controls_for(ActionId action) const {
    std::vector<Control> controls;
    if (action == ActionId::Invalid) {
        return controls;
    }
    for (const auto& [control, bound] : bindings_) {
        if (bound.action == action) {
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
    const auto held_key = down_keys_.find(control);
    if (held_key == down_keys_.end()) {
        return;
    }
    const WindowId window = held_key->second.window;
    down_keys_.erase(held_key);
    auto held = held_counts_.find(action);
    if (held != held_counts_.end()) {
        --held->second;
        if (held->second <= 0) {
            held_counts_.erase(held);
        }
    }
    if (ecs::World* const world = world_for(window)) {
        ecs::EventWriter<InputEvent>{*world}.send(InputEvent{action, InputEvent::Kind::Up, 0.f});
    }
}

void InputSystem::apply_digital(Control control, bool down, WindowId window) {
    const auto binding = bindings_.find(control);
    if (binding == bindings_.end()) {
        return;
    }

    const ActionId action = binding->second.action;
    const float scale = binding->second.scale;
    if (down) {
        if (down_keys_.contains(control)) {
            return;
        }
        down_keys_.emplace(control, Held{window, scale});
        ++held_counts_[action];
        if (ecs::World* const target = world_for(window)) {
            ecs::EventWriter<InputEvent>{*target}.send(InputEvent{action, InputEvent::Kind::Down, scale});
        }
        return;
    }

    release_held(control, action);
}

void InputSystem::handle_key(KeyCode key, bool down, bool repeat, WindowId window) {
    ecs::World* const world = world_for(window);
    if (world == nullptr) {
        return;
    }
    ecs::EventWriter<KeyEvent>{*world}.send(KeyEvent{
            .window = window,
            .key = key,
            .down = down,
            .repeat = repeat,
    });
    if (!repeat) {
        apply_digital(key_control(key), down, window);
    }
}

void InputSystem::handle_text_input(std::string_view text, WindowId window) {
    ecs::World* const world = world_for(window);
    if (world == nullptr) {
        return;
    }
    ecs::EventWriter<TextInputEvent>{*world}.send(TextInputEvent{
            .window = window,
            .text = std::string(text),
    });
}

void InputSystem::handle_text_editing(std::string_view text, int start, int length, WindowId window) {
    ecs::World* const world = world_for(window);
    if (world == nullptr) {
        return;
    }
    ecs::EventWriter<TextEditingEvent>{*world}.send(TextEditingEvent{
            .window = window,
            .text = std::string(text),
            .start = start,
            .length = length,
    });
}

void InputSystem::handle_mouse_button(
        WindowId window, MouseButton button, bool down, glm::vec2 position, std::uint8_t clicks) {
    ecs::World* const world = world_for(window);
    if (world == nullptr) {
        return;
    }
    ecs::EventWriter<MouseEvent>{*world}.send(MouseEvent{
            .window = window,
            .kind = down ? MouseEvent::Kind::Down : MouseEvent::Kind::Up,
            .position = position,
            .button = button,
            .clicks = clicks,
            .locked = is_mouse_locked(window),
    });
    if (button == MouseButton::None) {
        return;
    }
    apply_digital(mouse_control(button), down, window);
}

void InputSystem::handle_mouse_move(WindowId window, glm::vec2 position, glm::vec2 relative) {
    ecs::World* const world = world_for(window);
    if (world == nullptr) {
        return;
    }
    ecs::EventWriter<MouseEvent>{*world}.send(MouseEvent{
            .window = window,
            .kind = MouseEvent::Kind::Move,
            .position = position,
            .relative = relative,
            .locked = is_mouse_locked(window),
    });
    mouse_motion_[static_cast<std::size_t>(MouseAxis::X)] += relative.x;
    mouse_motion_[static_cast<std::size_t>(MouseAxis::Y)] += relative.y;
}

void InputSystem::handle_mouse_wheel(WindowId window, glm::vec2 position, float wheel_y) {
    ecs::World* const world = world_for(window);
    if (world == nullptr) {
        return;
    }
    ecs::EventWriter<MouseEvent>{*world}.send(MouseEvent{
            .window = window,
            .kind = MouseEvent::Kind::Wheel,
            .position = position,
            .wheel_y = wheel_y,
            .locked = is_mouse_locked(window),
    });
    mouse_motion_[static_cast<std::size_t>(MouseAxis::Wheel)] += wheel_y;
}

void InputSystem::handle_touch(std::uint32_t finger_id, bool down, glm::vec2 position) {
    if (world_for(kPrimaryWindow) == nullptr) {
        return;
    }
    if (down) {
        if (!primary_finger_.has_value()) {
            primary_finger_ = finger_id;
            handle_mouse_button(kPrimaryWindow, MouseButton::Left, true, position);
        }
        apply_digital(touch_control(finger_id), true, kPrimaryWindow);
        return;
    }
    apply_digital(touch_control(finger_id), false, kPrimaryWindow);
    if (primary_finger_ == finger_id) {
        handle_mouse_button(kPrimaryWindow, MouseButton::Left, false, position);
        primary_finger_.reset();
    }
}

void InputSystem::handle_touch_move(std::uint32_t finger_id, glm::vec2 position, glm::vec2 relative) {
    if (world_for(kPrimaryWindow) == nullptr) {
        return;
    }
    if (primary_finger_ == finger_id) {
        handle_mouse_move(kPrimaryWindow, position, relative);
    }
}

bool InputSystem::is_held(ActionId action) const {
    const auto it = held_counts_.find(action);
    return it != held_counts_.end() && it->second > 0;
}

float InputSystem::axis(ActionId action) const {
    if (action == ActionId::Invalid) {
        return 0.f;
    }
    float digital = 0.f;
    for (const auto& [control, held] : down_keys_) {
        const auto binding = bindings_.find(control);
        if (binding != bindings_.end() && binding->second.action == action) {
            digital += held.scale;
        }
    }
    float motion = 0.f;
    for (std::uint32_t index = 0; index < kMouseAxisCount; ++index) {
        const auto binding = bindings_.find(axis_control(static_cast<MouseAxis>(index)));
        if (binding != bindings_.end() && binding->second.action == action) {
            motion += mouse_motion_[index] * binding->second.scale;
        }
    }
    return std::clamp(digital, -1.f, 1.f) + motion;
}

std::optional<std::uint32_t> InputSystem::primary_touch_finger() const {
    return primary_finger_;
}

}
