# UI input

Pointer and keys become ECS events in [Input Mapper](Input%20Mapper.md). `run_input` (Frame / Input) is what turns those events into focus, commands, and text edits. The functions on `include/engine/ui/canvas.h` are the same operations for a test that does not go through `run_input`.

## Pointer path

`run_input` writes the pointer through `pointer_for`: `Presentation::pointer` for `kPrimaryWindow`, and `Presentation::pointers` for every other window. Move, Down, and Up store `position`. Down and Up store `down`.

| `MouseEvent::Kind` | Call |
| --- | --- |
| Down | `handle_pointer` with the button and `clicks` |
| Move | `update_pointer_hover`, and `update_text_selection` while the button is down |
| Up | `end_drag` and `end_pan` |
| Wheel | `handle_wheel` |

A right click does not place a caret and does not clear focus. `clicks` is the OS count from the button event.

`simulate_worlds` calls `reset_pointer_frame` before this phase. That clears `Presentation.mouse`. `begin_frame` does not. A hit inserts the window into `presentation_of(world).mouse`. Gameplay that should ignore that click reads `presentation_of(world).mouse.consumed_for(window)`. `world.ctx<ui::MouseConsumed>()` does not see the hits. `InputSystem` does not check either.

## What a hit is

An open `Popup` is above every canvas of its window. Canvases with an open popup are bound, laid out, and placed first, from highest `order` to lowest; the first with a popup under the point takes the event, even when the point is outside that canvas's rect. Otherwise canvases whose rect contains the point are tried from highest `order` to lowest. The top canvas is bound and laid out, then `hit_test`.

A hit from `hit_test_at` is any of these:

- a Button or a Checkbox
- an open Popup, anywhere inside it that no child takes
- a TextInput
- a ScrollView, even when it cannot scroll
- an element that `is_scrollable`: `overflow` `scroll`, or `auto` with `max_scroll > 0` on that axis. The scrollbar track is a hit before children
- an element with a bound `command` or `drag`
- a Viewport with a camera binding (`pan-x`, `pan-y`, or `zoom`)
- a Label whose `user-select` is `text` or `all`, when it is not under a Button or Checkbox

A plain Label, Stack, or Image does not consume the click.

Children are tested first, except that scrollbar track. A selectable Label is the only child suppressed under a Button or Checkbox, so `user-select: text` on that label does not steal the button. A Label with a bound `command` or `drag` stays that hit and is not a text selection.

Inline math does not consume the click. `label_text_selectable` (`src/ui/text_select.cpp`) still requires a `Label`, `user-select` other than `none`, no bound `command` or `drag`, and the label not `disabled`. On that label the formula is one source span, and the copy is that TeX.

Inside the canvas, siblings are visited front to back: the reverse of `child_stacking_order` (z-index ascending, document order on a tie). Containment uses `hit_bounds()`. For a rotated or scaled element that is the axis-aligned box of the transformed corners, which is slightly large at the corners. It is not an oriented-rect test.

A Viewport clips to its unpanned `layout_rect` and hit-tests children with the inverse camera (`origin + (pointer - origin) / zoom - pan`). Empty background hits the Viewport. That starts a pan only when the Viewport has a camera binding and `data_context` is set. A child Button still wins.

`handle_wheel` calls `find_scrollable_at` first. Over an open popup, only that popup is searched, and the wheel is consumed even when nothing there scrolls. A vertical scrollable changes `scroll_y` by `-wheel_y * 40` layout pixels, clamped to `0..max_scroll_y`, and returns. Horizontal `scroll_x` uses that same step, clamped to `0..max_scroll_x`, only when the element is not vertically scrollable. The element still updates when `data_context` is null. Only the binding write is skipped.

The zoom path runs only when that search misses and `data_context` is set. It then calls `find_viewport_at`, and continues only when that Viewport has `zoom` bound, so a control under the cursor still zooms the Viewport around it.

The new zoom is `clamp(z * pow(1.1, wheel_y), 0.25, 4)` (`kViewportZoomStep`, `kViewportMinZoom`, `kViewportMaxZoom`). Pan is rewritten so the content point under the cursor stays put. `viewport_zoom` turns a non-positive stored zoom into 1 before that.

The relayout uses `ctx<UiLayoutPainters>()`. The windowed presentation sets that to the window's NanoVG painter. Headless tests keep the CPU text measure in `document.cpp`.

`UiInputBatchCache` is only on `run_input`'s stack. The first event that touches a canvas binds and lays out. Later events in that same call reuse it. `handle_pointer` and the other public functions do not.

## Commands, checkbox, inspector

If the hit has a command and `can_execute()` is true, `execute()` runs.

A bound command sets `disabled` from `!can_execute()` on every bind, so `:disabled` matches. `TextInput` does not: its command is Enter-to-submit, and a disabled field ignores keys and text. Return still checks `can_execute()`.

A Checkbox hit flips `checked` before the command. `write_property_float` of `1` or `0` runs only when `checked` is bound and the canvas `data_context` is non-null. The target is `generated_owner` when that pointer is set (the row view-model inside an `ItemsControl`), otherwise `data_context`.

Return toggles `checked` only when the focused element is a Checkbox, then runs the command. `binding_target` is null without a `data_context`, so that float write uses the same guard. A click calls `clear_focus`. `set_focus` is only used for a TextInput or a selectable Label, so a click does not leave the Checkbox focused.

While the world's inspector is attached (the editor attaches the game world) and `pick_pointer` is set, a left click on a canvas uses `hit_test_visual`, records the path, inserts the window into `Presentation.mouse`, and returns before focus, drag, and `execute()`. Wheel is not intercepted. See [UI Inspector](UI%20Inspector.md).

## Popups

A press of any button outside the open popups of the window closes them (light dismiss) and does nothing else: no command, no focus, no drag. It is consumed. A popup stays open when the hit is inside it, or when the hit is its anchor: that click goes on to the anchor's command, so a toggle closes the menu instead of reopening it. A press inside a popup closes the popups it is not in, so a click in a menu closes its open submenu and still runs.

Escape closes the popup drawn last (the topmost canvas first) and stops there; it does not also clear focus outside it. A wheel outside every open popup closes them, then scrolls as usual.

Closing writes `0` through `open` (see [UI](../modules/UI.md#popup)) and clears focus when the focused element is inside that popup.

Inside a popup, `drag`, the scrollbar, and text selection use the element's shown position: the drag rect is moved by `popup_offset`, and `pointer_in_painted_space` restarts from the popup's offset instead of its anchor's scroll.

Input reads `open` as the frame's Bind left it, except that the canvas an event goes to is bound again first. A popup opened by the view-model shows and takes clicks from the next frame.

## Keyboard and text

`run_input` also drains `KeyEvent`, `TextInputEvent`, and `TextEditingEvent` into `ui::handle_key`, `handle_text_input`, and `handle_text_editing`.

Ctrl and Shift are `ctx<UiModifierState>()`, per window, from `LCtrl` / `RCtrl` / `LShift` / `RShift` on both down and up.

| Key | TextInput | Selectable Label |
| --- | --- | --- |
| character / Backspace / Delete | edits | ignored |
| Left / Right / Home / End | moves the caret | moves the caret across the whole string, not one visual row |
| Ctrl+Left / Ctrl+Right | not a word jump | not a word jump |
| Escape | clears focus | clears focus |
| Return | runs `command` when `can_execute()`; keeps focus | runs `command` when `can_execute()`; keeps focus |
| Ctrl+A | selects all | selects all |
| Ctrl+C | copies the selection | copies the selection, including `\n` between rows |
| Ctrl+X / Ctrl+V | cut and paste | do not change the text. Ctrl+X does not write the clipboard |

A `\n` inside a text-input event submits and clears focus. Physical Return submits and keeps focus.

`allow-copy="false"` blocks Ctrl+C on a Label and a TextInput. Cut checks `allow_copy`. `allow-paste` applies to TextInput only. Both attributes are literals.

The OS clipboard is `ctx<UiClipboard>()` (`set_text`, `get_text`). `SdlGlPresentation::poll` installs the SDL pair once. Tests install an in-memory pair. Empty functions are no-ops.

## Selection

`selection_anchor` is the fixed end. `caret_position` is the live end. A real selection is an anchor that differs from the caret.

- Click places the caret and sets the anchor equal to it.
- `clicks >= 2` selects the word (`word_range` in `src/ui/text_select.cpp`). `clicks >= 3` selects the whole string.
- `user-select: all` selects the whole string on the first click.
- Shift+click on an already focused element extends from the existing anchor. `set_focus` would clear the anchor, so the previous anchor is kept first. Shift+click on an unfocused element is an ordinary click.
- Drag (`update_text_selection` on Move) moves the live end. Granularity (character, word, or all) is `ctx<UiTextSelectGestures>()`, per window, until the next pointer down.
- Shift+arrows arm the anchor if it is unset, then move the caret.

A word is an approximation of UAX #29 with no Unicode library: Latin (including Latin-1 and Latin Extended), Greek and Coptic, Cyrillic U+0400–U+052F, and ASCII digits. An apostrophe (`'`, U+2019, U+02BC) or hyphen between two of those stays inside the word. A run of spaces is its own range. A run of punctuation is its own range. Every other code point, including CJK and emoji, is a one-character word.

Unshifted Left and Right collapse an existing selection to its edges (Left to the start, Right to the end) and set `selection_anchor` to `nullopt`. Unshifted Home and End move the caret to `0` and `text.size()` and clear the anchor.

On a TextInput, Backspace, Delete, and typing replace the range and clear the anchor, including a stale anchor that equals the caret. On a selectable Label, Backspace and Delete return immediately and leave the anchor in place. Losing focus still sets `selection_anchor` to `nullopt`.

A Label maps the click through `painted_text_lines` (glyph boxes in the pixels paint used, after `text-align` and `align-items`). Ancestor scroll and a Viewport camera are applied first. The element's own scroll is not: its text is drawn before its own pan. Vertical picks the row under the pointer, or the nearest row. Horizontal snaps to the nearer UTF-8 boundary. Characters the wrap dropped, and `\n`, are not highlighted, and they are part of the copied substring. Before the first paint the index is `text.size()`.

An inline formula is one atomic box, delimiters included. A click in the left half is before the opener. The right half is after the closer. Drag, double-click, and Left/Right take the whole span. `\\(` / `\\)` stay ordinary text. The painted box keeps a drawn-byte to source-byte map because the escape is shorter on screen.

A TextInput uses `caret_index_for_click` (x only). It measures prefixes with the font size and content origin paint stored, so the click matches the caret paint drew. The same fallback (`text.size()`) applies before the first paint or with no painter.

The highlight is `selection-color`, drawn before the glyphs, not tied to caret blink. Unset is a translucent blue.

## IME

Desktop `TextEditingEvent` is `Element::composition`, separate from the bound string. An empty editing event clears it. Text input commits and clears the preedit. While composition is non-empty, only Escape and Return are honored: Escape clears focus and the preedit, Return runs the command and drops the preedit without clearing focus. The Android SDL backend does not emit text-editing events.

The SDL text-input session follows a focused enabled TextInput only. `sync_frame` passes that field's window-pixel rect and caret offset to `SDL_SetTextInputArea` before `SDL_StartTextInput`, and refreshes the area every frame the session stays up.

## Drag and scroll

A drag starts only when `drag` is bound and `data_context` is set. It writes a 0–1 fraction through `write_property_float` as the pointer moves. `drag-orientation` picks the axis. A later move drops that drag if `data_context` is gone.

A pan starts only on a Viewport with a camera binding when `data_context` is set. `pan-x` and `pan-y` take deltas in layout pixels. A later move drops that pan if `data_context` is gone.

Scroll, drag, and pan write through `generated_owner` when that pointer is set, otherwise the canvas `data_context`. Scroll bindings (`scroll-x`, `scroll-y`) are read into the element clamped to `[0, max_scroll]` of the last layout, so a view-model may ask for the end with any value past it; the view-model keeps its own value, and a list that grew is clamped to its new extent on the next frame. They are written that way, including from a scrollbar drag. Wheel scroll still updates `scroll_x` / `scroll_y` on the element when `data_context` is null. Only the binding write is skipped.

## Tests

`tests/ui_layout_hit_test.cpp`, `tests/ui_text_input_test.cpp`, `tests/ui_label_select_test.cpp`, `tests/ui_scroll_test.cpp`, `tests/ui_input_batch_test.cpp`, `tests/mvvm_test.cpp`, `tests/ui_popup_test.cpp`.

## See also

- [UI Markup](UI%20Markup.md)
- [UI](../modules/UI.md)
- [Windowing](Windowing.md)
