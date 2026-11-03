# Input mapper

Gameplay reads `ActionId`. It does not switch on `KeyCode`. UI reads `MouseEvent`, `KeyEvent`, and the text events. Both come from `InputSystem`. Mouse look reads an action too: `MouseAxis` controls and `axis()` ([Axes](#axes-mouse-look-and-composites)), not `MouseEvent::relative` in a system ([Principles](../architecture/Principles.md) #4).

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
| `MouseMotion` | yes: `code` is a `MouseAxis` (`X`, `Y`, `Wheel`) |

`bind` accepts a `Control`, a `KeyCode`, a `MouseButton`, or a `MouseAxis`, either an `ActionId` or a name (the name overloads call `intern`), and a `scale` (default 1). `unbind` removes one control. `bound_action`, `bound_scale` (0 when unbound), and `controls_for` are the reverse lookups. Several controls may share one action. `bind` replaces the action and scale on that control. A `MouseMotion` code past `Wheel` is ignored, like `MouseButton::None`.

`reset()` forgets every binding, every interned name, every held control, the primary touch finger, the mouse motion of the frame, and the locked windows. It sends no `Up` events and keeps the router. The next `intern` returns `ActionId{1}` again. The editor calls it when a game stops.

## Axes: mouse look and composites

`axis(action)` is the value of an action this frame:

```
clamp(sum of `scale` over the held digital controls bound to it, -1, 1)
  + sum over the MouseAxis bindings of it of (this frame's motion on that axis * scale)
```

- Digital controls (keys, mouse buttons, fingers) count while held, with the scale they were bound with. Two keys with `+1` and `-1` on one action are a composite: `bind(KeyCode::D, move_x, 1)`, `bind(KeyCode::A, move_x, -1)`. Both held is 0. WASD is two such actions. Two keys with the same sign still give 1. The `Down` `InputEvent` carries the scale as `value`; `Up` carries 0.
- `MouseAxis::X` and `Y` sum `MouseEvent::relative` (SDL `xrel` / `yrel`, window pixels, +Y down) of every Move this frame; `Wheel` sums `wheel_y` (notches, + away from the user, a flipped wheel already turned round). The scale is the sensitivity (radians per pixel for a camera), a negative one inverts. This part is not clamped.
- Motion counts in relative mode and out of it (a right-drag orbit camera reads it with `is_held`). It counts from every window whose world the router returns, so it is global across windows like `is_held`: while a game plays in the editor, moving the free cursor over the editor window feeds it too. A dropped event (no world) adds nothing. The primary touch finger is the mouse, so its moves count.
- Motion sends no `InputEvent`, does not change `is_held`, and is not filtered by UI consumption, the same as `InputEvent`. A camera that should not turn while a drag is on the UI checks `presentation_of(world).mouse.consumed_for(window)`.

`begin_frame()` forgets the motion. `GameLoop::tick` calls it right before `IPresentation::poll` ([Runtime Loop](../architecture/Runtime%20Loop.md)), so `axis()` holds one frame's motion from the poll until the next frame, however often it is read. Read it on `Schedule::Frame`: a `Fixed` step would apply one frame's motion once per step (several times, or not at all on a frame with no step). The digital part is a state, so movement can read `axis()` on `Fixed`. `Host` owns no `InputSystem`; a test that feeds it calls `begin_frame` itself. `reentrant_tick` runs inside `poll` during a Windows modal move, so it reads the motion so far, which a window being moved by its title bar does not have.

## Locked mouse

`set_mouse_locked(window, locked)` and `is_mouse_locked(window)` say whether `window` is in relative mouse mode with focus ([Windowing](Windowing.md#relative-mouse)). The SDL presentation keeps it in step at the start of `poll` and on each focus event. Every `MouseEvent` of a locked window has `locked` set. The cursor is hidden there, so its `position` points at nothing:

| Consumer | Locked `Down`, `Move`, `Wheel` | Locked `Up` |
| --- | --- | --- |
| `run_input` (UI) | skipped: no hover, press, scroll, or consumption | ends the press, drag, and pan |
| `run_pointer_targets` | skipped; nothing stays hovered in that window | ends a press, without a `Click` |
| `run_dock_input` | skipped | handled |
| a game system | sees it, `relative` included | sees it |

`consumed_for(window)` therefore stays false while the mouse is locked, so a click always reaches gameplay. Bound mouse buttons send `InputEvent` as usual.

`KeyCode` values match SDL3 scancodes. The header does not include SDL. `AcBack` is the Android back key (scancode 282).

## Android back

The engine never quits on `AcBack`. `SDL_HINT_ANDROID_TRAP_BACK_BUTTON` is set before `SDL_Init` (and again from the Android manifest as `SDL_ENV.SDL_ANDROID_TRAP_BACK_BUTTON`), and the template manifest sets `android:enableOnBackInvokedCallback="true"`. On Android 13 and later the system Back gesture is that callback, not a key and not `onBackPressed`. SDL registers the callback on the main thread and turns it into `AcBack`. Without the attribute the system finishes the activity, `SDL_EVENT_QUIT` follows, and the game never sees the key. A game binds `AcBack` to its own `ActionId`, pops its screens, and calls `ApplicationState::quit()` from its root screen. Unbound, Back does nothing. This is the same model as `WindowCloseRequestedEvent` ([Windowing](Windowing.md#close)).

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
| `handle_mouse_move` | `MouseEvent` Move. Adds `relative` to the `MouseAxis::X` / `Y` motion |
| `handle_mouse_wheel` | `MouseEvent` Wheel with `wheel_y`. Adds it to the `MouseAxis::Wheel` motion |
| `handle_text_input` | `TextInputEvent` |
| `handle_text_editing` | `TextEditingEvent` (`start` and `length` are code points; -1 is unset) |
| `handle_touch` | `InputEvent` for `ControlKind::Touch` and that finger id. The first finger also synthesizes left-button Down/Up on `kPrimaryWindow` |
| `handle_touch_move` | Move on `kPrimaryWindow` while that finger is the primary one |

`MouseEvent::clicks` is the OS click count (1, 2, 3+). Move and wheel stay at 1. `is_held` is true while at least one control bound to that action is down. `primary_touch_finger` is the finger currently synthesizing the mouse, if any.

`denormalize_touch` maps a 0–1 finger position onto a drawable size. Negative drawable extents are treated as 0.

Poll does not look at `Presentation.mouse`. UI has not run yet; `run_pointer_targets` (hover, press, and click on `PointerTarget` entities) runs after it, in the same Input phase ([UI Input](UI%20Input.md#pointer-targets)). A game system on `Phase::Game` that should ignore a click the UI ate checks `presentation_of(world).mouse.consumed_for(window)`. `world.ctx<ui::MouseConsumed>()` does not see those hits.

## Where SDL is decoded

`SdlGlPresentation::poll` (`src/render/opengl/sdl_gl_presentation.cpp`) maps keyboard, mouse, text, editing, finger, quit, and window-size events onto these handlers. Keys pass through `BackKeyFilter` first ([Android back](#android-back)). Focus events drive relative mouse mode and the locked windows ([Locked mouse](#locked-mouse)). Gamepad events are not dispatched.

`run_input` later drains `MouseEvent` and `KeyEvent` into the UI. See [UI Input](UI%20Input.md).

## Not built

The same `Control` / `ActionId` / `InputEvent` types are the place a gamepad poll or an action map would plug in. Neither exists: `GamepadButton` and `GamepadAxis` can be bound but nothing sends them; a gamepad axis would add `value * scale` to the clamped part of `axis()`. Composites are signed scales ([Axes](#axes-mouse-look-and-composites)), not a separate type. There is no callback when an action fires and no `.inputactions` file.

## Files

- `include/engine/core/input_system.h`
- `include/engine/core/key_code.h`
- `src/core/input_system.cpp`
- `src/core/back_key_filter.h`, `src/core/back_key_filter.cpp`
- `src/render/opengl/sdl_gl_presentation.cpp`
- `tests/input_test.cpp` (binds, holds, touch, `reset`, mouse axes and their frame, composites and clamping, locked events)
- `tests/game_loop_test.cpp` (`begin_frame` before `poll`)
- `tests/pointer_target_test.cpp` (locked events reach neither the UI nor a target)
- `tests/android_lifecycle_test.cpp` (`BackKeyFilter`)

## See also

- [Core](../modules/Core.md)
- [UI Input](UI%20Input.md)
