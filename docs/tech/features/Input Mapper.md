---
tags: [feature]
---

# Input Mapper

## Bindings

Gameplay binds named controls, not raw keys: `InputSystem::intern(name) -> ActionId`, then `bind(Control, ActionId)` / `bind(KeyCode, ActionId)` / `bind(MouseButton, ActionId)` (string-name overloads also exist, interning internally). `Control` is `{ControlKind, code, device}` — `Key`, `MouseButton`, `GamepadButton`, `GamepadAxis`, `Touch`. `KeyCode` is a named enum matching SDL3 scancode values ([[include.engine.core.key_code.h]]); no SDL headers in game code.

`handle_key` / `handle_mouse_button` / `handle_mouse_move` emit `InputEvent` (by `ActionId`, if bound) / `MouseEvent` (always, for UI/pick) on the world ([[include.engine.core.input_system.h]]). Touch: `handle_touch` / `handle_touch_move` — the primary finger also synthesizes left-mouse Down/Move/Up for UI; a bound `ControlKind::Touch` emits `InputEvent`. `is_held(ActionId)` queries held state. Remap queries: `bound_action(Control|KeyCode|MouseButton)`, `controls_for(ActionId)`, `unbind(...)`.

`InputSystem` does not filter `InputEvent` on `MouseConsumed` — UI has not run yet at poll time. Gameplay in `Phase::Game` must check `world.ctx<ui::MouseConsumed>().consumed_for(window)` itself before treating a mouse-bound action as a world action.

Windowed poll: [[src.core.engine_runtime.cpp]] maps SDL keyboard, mouse, and finger events → these handlers. Also quit and resize. Gamepad SDL events are not polled yet.

## Later

Same `Control` / `ActionId` / `InputEvent` types: wire gamepad button/axis from SDL, WASD composites, action maps. Not a Unity Input System clone (no action callbacks, no `.inputactions`).

## Files

- [[include.engine.core.input_system.h]]
- [[src.core.input_system.cpp]]
- [[src.core.engine_runtime.cpp]]
- [[tests.input_test.cpp]]

## See also

- [[features/UI Input]]
- [[modules/Core]]
- [[features/Events]]
