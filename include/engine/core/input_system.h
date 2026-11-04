#pragma once

// docs/tech/features/Input Mapper.md

#include <engine/core/key_code.h>
#include <engine/core/window_desc.h>
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
#include <vector>

namespace engine {

enum class MouseButton : std::uint8_t { None = 0, Left = 1, Middle = 2, Right = 3 };

enum class ActionId : std::uint32_t { Invalid = 0 };

enum class ControlKind : std::uint8_t {
    Key = 1,
    MouseButton = 2,
    GamepadButton = 3,
    GamepadAxis = 4,
    Touch = 5,
    // Mouse motion and wheel as an axis: `code` is a MouseAxis.
    MouseMotion = 6,
};

// The axes of ControlKind::MouseMotion. X and Y are pixels of motion (+Y down), Wheel is notches (+ away from the user).
enum class MouseAxis : std::uint8_t { X = 0, Y = 1, Wheel = 2 };

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

// Down and Up of a digital control (key, mouse button, finger). `value` is the binding's scale on Down, 0 on Up.
// Mouse motion sends none: read it with InputSystem::axis.
struct InputEvent {
    ActionId action{};
    enum class Kind { Down, Up } kind = Kind::Down;
    float value = 0.f;
};

struct MouseEvent {
    WindowId window = kPrimaryWindow;
    enum class Kind { Down, Up, Move, Wheel } kind = Kind::Move;
    glm::vec2 position{};
    glm::vec2 relative{};
    MouseButton button = MouseButton::None;
    float wheel_y = 0.f;
    // OS click count from the button event (1 single, 2 double, 3+ triple). Move and wheel stay 1.
    std::uint8_t clicks = 1;
    // The window is in relative mouse mode (IWindowControl::set_relative_mouse) and has focus: the cursor is hidden,
    // so `position` points at nothing. The UI and pointer targets skip Down, Move, and Wheel while it is set.
    bool locked = false;
};

struct KeyEvent {
    WindowId window = kPrimaryWindow;
    KeyCode key = KeyCode::Unknown;
    bool down = false;
    bool repeat = false;
};

struct TextInputEvent {
    WindowId window = kPrimaryWindow;
    std::string text;
};

// IME composition (SDL_EVENT_TEXT_EDITING). start and length are Unicode code points into text;
// -1 means unset. The committed characters arrive later as TextInputEvent.
struct TextEditingEvent {
    WindowId window = kPrimaryWindow;
    std::string text;
    int start = -1;
    int length = -1;
};

[[nodiscard]] inline glm::vec2 denormalize_touch(glm::vec2 normalized, glm::ivec2 drawable) {
    const float width = static_cast<float>(drawable.x < 0 ? 0 : drawable.x);
    const float height = static_cast<float>(drawable.y < 0 ? 0 : drawable.y);
    return {normalized.x * width, normalized.y * height};
}

class InputSystem {
public:
    InputSystem() = default;

    ActionId intern(std::string_view name);
    [[nodiscard]] std::optional<ActionId> find(std::string_view name) const;
    [[nodiscard]] std::string_view debug_name(ActionId action) const;

    // `scale` is what the control adds to axis(action): +1 and -1 on two keys make a composite (D +1, A -1 on
    // "move_x"), and on a MouseAxis it is the sensitivity (a negative one inverts).
    void bind(Control control, ActionId action, float scale = 1.f);
    void bind(KeyCode key, ActionId action, float scale = 1.f);
    void bind(KeyCode key, std::string_view name, float scale = 1.f);
    void bind(MouseButton button, ActionId action, float scale = 1.f);
    void bind(MouseButton button, std::string_view name, float scale = 1.f);
    void bind(MouseAxis axis, ActionId action, float scale = 1.f);
    void bind(MouseAxis axis, std::string_view name, float scale = 1.f);

    void unbind(Control control);
    void unbind(KeyCode key);
    void unbind(MouseButton button);
    void unbind(MouseAxis axis);

    [[nodiscard]] ActionId bound_action(Control control) const;
    [[nodiscard]] ActionId bound_action(KeyCode key) const;
    [[nodiscard]] ActionId bound_action(MouseButton button) const;
    [[nodiscard]] ActionId bound_action(MouseAxis axis) const;
    // The scale `control` was bound with; 0 when it is unbound.
    [[nodiscard]] float bound_scale(Control control) const;
    [[nodiscard]] std::vector<Control> controls_for(ActionId action) const;

    void handle_key(KeyCode key, bool down, bool repeat = false, WindowId window = kPrimaryWindow);
    void handle_text_input(std::string_view text, WindowId window = kPrimaryWindow);
    void handle_text_editing(std::string_view text, int start, int length, WindowId window = kPrimaryWindow);
    void handle_mouse_button(
            WindowId window, MouseButton button, bool down, glm::vec2 position, std::uint8_t clicks = 1);
    void handle_mouse_move(WindowId window, glm::vec2 position, glm::vec2 relative);
    void handle_mouse_wheel(WindowId window, glm::vec2 position, float wheel_y);
    void handle_touch(std::uint32_t finger_id, bool down, glm::vec2 position);
    void handle_touch_move(std::uint32_t finger_id, glm::vec2 position, glm::vec2 relative);

    [[nodiscard]] bool is_held(ActionId action) const;

    // This frame's value of `action`: the scales of the held digital controls bound to it, summed and clamped to
    // [-1, 1], plus the mouse motion of this frame times the scale of each MouseAxis bound to it (not clamped).
    // Motion counts from every window that has a world, locked or not, and is not filtered by UI consumption.
    // Read it on Schedule::Frame: a Fixed step would apply one frame's motion once per step.
    [[nodiscard]] float axis(ActionId action) const;

    // Starts an input frame: forgets the mouse motion summed since the last call. GameLoop calls it right before
    // the presentation polls. A test, or a game driven by Host, calls it itself.
    void begin_frame();

    // Whether `window`'s mouse events go out with MouseEvent::locked. The presentation keeps this in step with
    // relative mouse mode.
    void set_mouse_locked(WindowId window, bool locked);
    [[nodiscard]] bool is_mouse_locked(WindowId window) const;

    [[nodiscard]] std::optional<std::uint32_t> primary_touch_finger() const;

    // Events for a window go to the world this returns. A null world drops the event.
    void set_router(std::function<ecs::World*(WindowId)> router);

    // Forgets every binding, interned action, held control, the primary touch finger, this frame's mouse motion,
    // and the locked windows. Sends no Up events. The router stays. The editor calls this on Stop, so the next game
    // interns from 1 again.
    void reset();

private:
    struct Binding {
        ActionId action{};
        float scale = 1.f;
    };
    struct Held {
        WindowId window = kPrimaryWindow;
        float scale = 1.f;
    };

    [[nodiscard]] ecs::World* world_for(WindowId window) const;
    void apply_digital(Control control, bool down, WindowId window);
    void release_held(Control control, ActionId action);

    std::function<ecs::World*(WindowId)> router_;
    std::optional<std::uint32_t> primary_finger_;
    std::deque<std::string> interned_names_;
    std::unordered_map<std::string_view, ActionId> name_to_id_;
    std::unordered_map<Control, Binding> bindings_;
    std::unordered_map<Control, Held> down_keys_;
    std::unordered_map<ActionId, int> held_counts_;
    // Indexed by MouseAxis.
    float mouse_motion_[3]{};
    std::unordered_set<WindowId> locked_;
};

}
