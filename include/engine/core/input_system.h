#pragma once

#include <engine/ecs/world.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace engine {

enum class KeyCode : std::uint32_t { Unknown = 0 };

enum class MouseButton : std::uint8_t { None = 0, Left = 1, Middle = 2, Right = 3 };

struct InputEvent {
    std::string action;
    enum class Kind { Down, Up } kind = Kind::Down;
};

struct MouseEvent {
    enum class Kind { Down, Up, Move } kind = Kind::Move;
    glm::vec2 position{};
    glm::vec2 relative{};
    MouseButton button = MouseButton::None;
};

class InputSystem {
public:
    explicit InputSystem(ecs::World& world);

    void bind(KeyCode key, std::string action);
    void handle_key(KeyCode key, bool down);
    void handle_mouse_button(MouseButton button, bool down, glm::vec2 position);
    void handle_mouse_move(glm::vec2 position, glm::vec2 relative);

    [[nodiscard]] bool is_held(std::string_view action) const;

private:
    ecs::World* world_ = nullptr;
    std::unordered_map<std::uint32_t, std::string> bindings_;
    std::unordered_set<std::uint32_t> down_keys_;
    std::unordered_map<std::string, int> held_counts_;
};

}
