#pragma once

#include <engine/ecs/world.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace engine {

enum class KeyCode : std::uint32_t { Unknown = 0 };

enum class MouseButton : std::uint8_t { None = 0, Left = 1, Middle = 2, Right = 3 };

enum class ActionId : std::uint32_t { Invalid = 0 };

enum class ControlKind : std::uint8_t {
    Key = 1,
    MouseButton = 2,
    GamepadButton = 3,
    GamepadAxis = 4,
    Touch = 5,
};

struct Control {
    ControlKind kind{};
    std::uint32_t code = 0;
    std::uint32_t device = 0;
    constexpr bool operator==(const Control&) const noexcept = default;
};

}

template <>
struct std::hash<engine::Control> {
    std::size_t operator()(const engine::Control& control) const noexcept {
        std::size_t h = std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(control.kind));
        h ^= std::hash<std::uint32_t>{}(control.code) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::uint32_t>{}(control.device) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

namespace engine {

struct InputEvent {
    ActionId action{};
    enum class Kind { Down, Up } kind = Kind::Down;
    float value = 0.f;
};

struct MouseEvent {
    enum class Kind { Down, Up, Move } kind = Kind::Move;
    glm::vec2 position{};
    glm::vec2 relative{};
    MouseButton button = MouseButton::None;
};

class InputSystem {
public:
    InputSystem() = default;
    explicit InputSystem(ecs::World& world);

    void set_world(ecs::World& world);

    ActionId intern(std::string_view name);
    [[nodiscard]] std::optional<ActionId> find(std::string_view name) const;
    [[nodiscard]] std::string_view debug_name(ActionId action) const;

    void bind(Control control, ActionId action);
    void bind(KeyCode key, ActionId action);
    void bind(KeyCode key, std::string_view name);

    void handle_key(KeyCode key, bool down);
    void handle_mouse_button(MouseButton button, bool down, glm::vec2 position);
    void handle_mouse_move(glm::vec2 position, glm::vec2 relative);

    [[nodiscard]] bool is_held(ActionId action) const;

private:
    ecs::World* world_ = nullptr;
    std::deque<std::string> interned_names_;
    std::unordered_map<std::string_view, ActionId> name_to_id_;
    std::unordered_map<Control, ActionId> bindings_;
    std::unordered_set<Control> down_keys_;
    std::unordered_map<ActionId, int> held_counts_;
};

}
