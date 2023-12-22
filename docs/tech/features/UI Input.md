---
tags: [feature]
---

# UI Input

## Pointer

`run_input` copies `MouseEvent` into `UiPointer`, on Down calls `ui::handle_pointer` ([[src.ui.canvas.cpp]]). Wheel calls `ui::handle_wheel`. Up ends both `ActiveDrag` and `ActivePan`.

## Hit-test

Canvases whose `rect` contains the point, sorted by `order` descending (higher = front). The top canvas is rebound and relaid out, then `hit_test` ([[src.ui.document.cpp]], shared with `paint.cpp`'s `:hover` resolution) looks for a **Button**, a **Checkbox**, or any element with a bound `command` or `drag`, or a Viewport with a camera binding; a plain Label/Stack/Image does not consume.

Entering a Viewport clips to its unpanned `layout_rect` and hit-tests children with the inverse camera (`O + (pointer - O) / Z - P`). Empty background therefore hits the Viewport (pan drag). A child Button still wins. Wheel zoom uses `find_viewport_at` so a node under the cursor still zooms its enclosing Viewport (writes `zoom` and pan so the content point stays put). Zoom is clamped to `[0.25, 4]`.

That relayout uses the same per-window `IUiPainter` as paint (`world.ctx<UiLayoutPainters>()`, set by `EngineRuntime` from each window's NanoVG painter after `MakeCurrent`). Hug text then matches glyphs. No painter registered (headless `engine_tests`, a window created without a UI painter) keeps the CPU fallback in [[src.ui.document.cpp]].

Within a canvas, `hit_test` visits siblings topmost-first — the reverse of `child_stacking_order()` (z-index ascending, document-order tie-break), so it always agrees with paint order: the element drawn on top is also the one that receives the click. Its containment check uses `hit_bounds()`, not the raw `layout_rect` — for a rotated/scaled element (`transform: rotate() scale()`) that's the axis-aligned bounding box of the transformed corners, an approximation (slightly generous at a rotated element's corners), not a precise oriented-rect test.

If `can_execute()`, `Execute()`.

A hit on a **Checkbox** additionally flips its `checked` bool (before command dispatch, so a bound `command` still fires too) and writes it back through `checked="{binding}"` if bound — same `generated_owner`-aware target resolution (item VM inside an `ItemsControl` vs. the canvas `data_context`) as `drag`/`scroll-y` write-back. `KeyCode::Return` on the focused element mirrors this.

`begin_frame` resets `MouseConsumed` and reapplies FillWindow rects.

Gameplay **must** read `world.ctx<ui::MouseConsumed>().consumed_for(window)` before treating a click as a world pick.

## Keyboard editing & clipboard

`ui::handle_key`/`ui::handle_text_input` ([[src.ui.canvas.cpp]]) drive the focused `TextInput`'s
`caret_position`, UTF-8-aware (`prev_utf8_char`/`next_utf8_char`). `Backspace`/`Delete` edit one
character; `Left`/`Right`/`Home`/`End` move the caret; `Escape` clears focus; `Return` toggles a
focused Checkbox and/or executes a bound `command`.

Ctrl/Shift are tracked in `world.ctx<ui::UiModifierState>()`, keyed by `WindowId`, updated by
`handle_key` special-casing `LCtrl`/`RCtrl`/`LShift`/`RShift` on both down **and** up — this stays
inside the SDL-free `ui` module rather than widening `KeyEvent`.

### Selection

`Element::selection_anchor` (`std::optional<std::size_t>`) is the fixed end of an in-progress
selection; `caret_position` is always the live end. A real (non-collapsed) selection is
`selection_anchor.has_value() && *selection_anchor != caret_position`, spanning
`[min(*anchor, caret), max(...))`. Set by:

- **Click** (`handle_pointer_impl`) — places the caret at the clicked glyph and sets
  `anchor = caret = that index` (no selection yet, but armed for a drag).
- **Drag** — `ui::update_text_selection` (called from `run_input`'s `MouseEvent::Kind::Move`
  branch, [[src.ecs.systems.cpp]], whenever the button is still down) moves `caret_position` to
  the glyph under the pointer, leaving `selection_anchor` at the drag's start. No re-hit-test and
  no `ActiveDrag`-style ctx state, unlike a `drag="{binding}"` slider: this only ever mutates the
  already-focused `Element`'s own fields, so `UiFocusState` is enough to find it again each Move.
- **Ctrl+A** — `anchor = 0`, `caret = text.size()`.
- **Shift+Left/Right/Home/End** — arms `anchor = caret_position` first if not already set, then
  moves the caret without collapsing.

Every other caret-moving or text-editing operation collapses/clears it: unshifted
`Left`/`Home` move to the selection's start, `Right`/`End` to its end (instead of one more
character); `Backspace`/`Delete`/typing replace the whole selected range. Any of these — and
losing focus — resets `selection_anchor` to `nullopt`, including when it was merely stale
(anchor == caret, e.g. right after a click that never became a drag) — left set, a later move
would make it look like a real selection that was never actually made.

Click-to-caret-index (`caret_index_for_click`, canvas.cpp) walks UTF-8 boundaries
(`next_utf8_char`) comparing each prefix's `IUiPainter::measure_text` width against the click x,
snapping to the nearer boundary. It reads back `Element::painted_font_size_px`/
`painted_content_origin_x` — cached by `paint.cpp`'s `TextInput` block every time it paints,
regardless of focus — rather than re-resolving CSS length units (padding, em font-size) itself, so
a click can never land somewhere paint didn't actually draw the caret. Falls back to
`text.size()` before the element has ever painted (e.g. a click on the very first frame) or with
no painter registered (headless `engine_tests` without a fake `UiLayoutPainters`).

The highlight itself paints in `paint.cpp`'s `TextInput` block, behind the caret, whenever a real
selection exists — not gated on the caret's blink phase. Its color is the cascaded
`selection-color` CSS property (`Element::selection_color`/`ComputedStyle::has_selection_color`),
resolved the same way as `scrollbar-thumb-color`; unset defaults to a translucent blue.

### Clipboard

**Ctrl+C** copies the selected substring to the OS clipboard (a no-op with no selection, same as a
real text field); **Ctrl+X** does the same then erases it; **Ctrl+V** pastes clipboard text at the
caret, replacing the selection first if one exists. The OS clipboard itself is reached through
`world.ctx<ui::UiClipboard>()` — two `std::function`s (`set_text`/`get_text`), empty (no-op)
unless installed. `EngineRuntime::poll_events` installs the real SDL-backed pair once, lazily
([[src.render.opengl.clipboard.h]]/`.cpp`, since SDL's clipboard is process-global, not
per-window); `engine_tests` installs an in-memory fake per test
([[tests.ui_text_input_test.cpp]]) so clipboard behavior is covered without a real window.

`Element::allow_copy`/`allow_paste` (XML `allow-copy`/`allow-paste`, default both `true`, literal
only — no `{binding}`) lock a field's clipboard access per-operation, mirroring how a web page
blocks copy/cut/paste independently by intercepting those DOM events rather than one combined
on/off switch. Cut is gated by `allow_copy` (it reads before deleting), not `allow_paste`.

## Files

- [[include.engine.ui.canvas.h]]
- [[src.ui.canvas.cpp]]
- [[src.ecs.systems.cpp]]
- [[src.render.opengl.clipboard.h]]
- [[tests.mvvm_test.cpp]]
- [[tests.ui_layout_hit_test.cpp]]
- [[tests.ui_text_input_test.cpp]]

## See also

- [[features/UI Markup]]
- [[features/Input Mapper]]
- [[modules/UI]]
