---
tags: [feature]
---

# Input Mapper

## Bindings

Gameplay binds named controls, not raw keys: `InputSystem::intern(name) -> ActionId`, then `bind(Control, ActionId)` / `bind(KeyCode, ActionId)` / `bind(MouseButton, ActionId)` (string-name overloads also exist, interning internally). `Control` is `{ControlKind, code}` (`ControlKind::Key`/`MouseButton`, more later) — one table, not a parallel map per device. `KeyCode` is a named enum matching SDL3 scancode values ([[include.engine.core.key_code.h]]); no SDL headers in game code.

`handle_key` / `handle_mouse_button` / `handle_mouse_move` emit `InputEvent` (by `ActionId`, if bound) / `MouseEvent` (always, for UI/pick) on the world ([[include.engine.core.input_system.h]]). `is_held(ActionId)` queries held state for keys and bound mouse buttons. Remap queries: `bound_action(Control|KeyCode|MouseButton)`, `controls_for(ActionId)`, `unbind(...)`.

`InputSystem` does not filter `InputEvent` on `MouseConsumed` — UI has not run yet at poll time. Gameplay in `Phase::Game` must check `world.ctx<ui::MouseConsumed>().value` itself before treating a mouse-bound action as a world action.

Windowed poll: [[src.core.engine_runtime.cpp]] maps SDL events → these handlers. Also quit and resize.

## Later

Keyboard and mouse-button binds are done. Not yet implemented, same `Control`/`ActionId`/`InputEvent` types: gamepad buttons/axes, touch, WASD composites, action maps, `MouseEvent` → `PointerEvent` rename. Not a Unity Input System clone (no action callbacks, no `.inputactions`).

## Files

- [[include.engine.core.input_system.h]]
- [[src.core.input_system.cpp]]
- [[src.core.engine_runtime.cpp]]
- [[tests.input_test.cpp]]

## See also

- [[features/UI Input]]
- [[modules/Core]]
- [[features/Events]]
