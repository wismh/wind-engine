# Input mapper

Gameplay reads `ActionId`. It does not switch on `KeyCode`. UI reads `MouseEvent`, `KeyEvent`, and the text events. Both come from `InputSystem`.

## Names and controls

`intern(name)` returns a stable `ActionId` for that string. `Invalid` is 0 and means "not bound". `find` returns `nullopt` when the name was never interned. `debug_name` is the interned string.

`Control` is `kind`, `code`, and `device`.

| `ControlKind` | Wired from SDL poll |
| --- | --- |
| `Key` | yes |
| `MouseButton` | yes |
| `Touch` | yes |
| `GamepadButton` | no |
| `GamepadAxis` | no |

`bind` accepts a `Control`, a `KeyCode`, or a `MouseButton`, and either an `ActionId` or a name (the name overloads call `intern`). `unbind` removes one control. `bound_action` and `controls_for` are the reverse lookups. Several controls may share one action. `bind` replaces the action on that control.

`reset()` forgets every binding, every interned name, every held control, and the primary touch finger. It sends no `Up` events and keeps the router. The next `intern` returns `ActionId{1}` again. The editor calls it when a game stops.

`KeyCode` values match SDL3 scancodes. The header does not include SDL. `AcBack` is the Android back key (scancode 282).

## Android back

The engine never quits on `AcBack` (`SDL_HINT_ANDROID_TRAP_BACK_BUTTON` is on, so SDL does not either). A game binds `AcBack` to its own `ActionId`, pops its screens, and calls `ApplicationState::quit()` from its root screen. Unbound, Back does nothing. This is the same model as `WindowCloseRequestedEvent` ([Windowing](Windowing.md#close)).

`poll` asks `BackKeyFilter` (`src/core/back_key_filter.h`) about every key before `handle_key`:

| Key event | Route |
| --- | --- |
| any key other than `AcBack` | `Deliver` |
| `AcBack` press, no text input active on that window | `Deliver` |
| `AcBack` press, text input active | `DismissTextInput`: `ui::clear_focus` on that window, not delivered |
| repeat or release of a press that dismissed | `Swallow`: not delivered |
| repeat or release of a delivered press | `Deliver` |

A dismissing press sends no `KeyEvent` and no `InputEvent`, so one press does not both close the keyboard and navigate back.

## What `handle_*` sends

The system must have `set_router`. A handler asks the router for the world bound to that event's window. A null world drops the event. Keyboard and text go to the window the OS delivered. Touch stays aimed at `kPrimaryWindow`.

| Handler | ECS events |
| --- | --- |
| `handle_key` | `KeyEvent` always, including repeats. `InputEvent` Down/Up only when the key is bound and `repeat` is false |
| `handle_mouse_button` | `MouseEvent` Down/Up. `InputEvent` when that button is bound |
| `handle_mouse_move` | `MouseEvent` Move |
| `handle_mouse_wheel` | `MouseEvent` Wheel with `wheel_y` |
| `handle_text_input` | `TextInputEvent` |
| `handle_text_editing` | `TextEditingEvent` (`start` and `length` are code points; -1 is unset) |
| `handle_touch` | `InputEvent` for `ControlKind::Touch` and that finger id. The first finger also synthesizes left-button Down/Up on `kPrimaryWindow` |
| `handle_touch_move` | Move on `kPrimaryWindow` while that finger is the primary one |

`MouseEvent::clicks` is the OS click count (1, 2, 3+). Move and wheel stay at 1. `is_held` is true while at least one control bound to that action is down. `primary_touch_finger` is the finger currently synthesizing the mouse, if any.

`denormalize_touch` maps a 0–1 finger position onto a drawable size. Negative drawable extents are treated as 0.

Poll does not look at `Presentation.mouse`. UI has not run yet. A game system on `Phase::Game` that should ignore a click the UI ate checks `presentation_of(world).mouse.consumed_for(window)`. `world.ctx<ui::MouseConsumed>()` does not see those hits.

## Where SDL is decoded

`SdlGlPresentation::poll` (`src/render/opengl/sdl_gl_presentation.cpp`) maps keyboard, mouse, text, editing, finger, quit, and window-size events onto these handlers. Keys pass through `BackKeyFilter` first ([Android back](#android-back)). Gamepad events are not dispatched.

`run_input` later drains `MouseEvent` and `KeyEvent` into the UI. See [UI Input](UI%20Input.md).

## Not built

The same `Control` / `ActionId` / `InputEvent` types are the place a gamepad poll, a WASD composite, or an action map would plug in. None of those exist. There is no callback when an action fires and no `.inputactions` file.

## Files

- `include/engine/core/input_system.h`
- `include/engine/core/key_code.h`
- `src/core/input_system.cpp`
- `src/core/back_key_filter.h`, `src/core/back_key_filter.cpp`
- `src/render/opengl/sdl_gl_presentation.cpp`
- `tests/input_test.cpp`
- `tests/android_lifecycle_test.cpp` (`BackKeyFilter`)

## See also

- [Core](../modules/Core.md)
- [UI Input](UI%20Input.md)
