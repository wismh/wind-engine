---
tags: [feature]
---

# UI Input

## Pointer

`run_input` copies `MouseEvent` into `UiPointer`, on Down calls `ui::handle_pointer` ([[src.ui.canvas.cpp]]). Wheel calls `ui::handle_wheel`. Up ends both `ActiveDrag` and `ActivePan`. `MouseEvent::clicks` is `SDL_MouseButtonEvent.clicks` (the OS double-click interval); Down passes the left button and that count into `handle_pointer`. A right click does not place a caret and does not clear focus.

## Hit-test

Canvases whose `rect` contains the point, sorted by `order` descending (higher = front). The top canvas is rebound and relaid out, then `hit_test` ([[src.ui.document.cpp]], shared with `paint.cpp`'s `:hover` resolution) looks for a **Button**, a **Checkbox**, any element with a bound `command` or `drag`, a Viewport with a camera binding, or a **Label** whose `user-select` is `text` or `all`. A plain Label/Stack/Image does not consume. A Label inside a Button or Checkbox is not its own hit (children are tested first, so `Label { user-select: text }` must not steal the control). A Label with a bound `command` or `drag` stays that hit and is not selectable. A Label whose text contains an inline formula is a hit too: the formula is one source span, `\(...\)` included, and the copy is that TeX.

Entering a Viewport clips to its unpanned `layout_rect` and hit-tests children with the inverse camera (`O + (pointer - O) / Z - P`). Empty background therefore hits the Viewport (pan drag). A child Button still wins. Wheel zoom uses `find_viewport_at` so a node under the cursor still zooms its enclosing Viewport (writes `zoom` and pan so the content point stays put). Zoom is clamped to `[0.25, 4]`.

That relayout uses the same per-window `IUiPainter` as paint (`world.ctx<UiLayoutPainters>()`, set by `EngineRuntime` from each window's NanoVG painter after `MakeCurrent`). Hug text then matches glyphs. No painter registered (headless `engine_tests`, a window created without a UI painter) keeps the CPU fallback in [[src.ui.document.cpp]].

Within a canvas, `hit_test` visits siblings topmost-first — the reverse of `child_stacking_order()` (z-index ascending, document-order tie-break), so it always agrees with paint order: the element drawn on top is also the one that receives the click. Its containment check uses `hit_bounds()`, not the raw `layout_rect` — for a rotated/scaled element (`transform: rotate() scale()`) that's the axis-aligned bounding box of the transformed corners, an approximation (slightly generous at a rotated element's corners), not a precise oriented-rect test.

If `can_execute()`, `Execute()`.

A bound `command` sets `disabled` from `!can_execute()` on every bind, so a Button matches `:disabled` while the command cannot run. `TextInput` is the exception: its command is Enter-to-submit, and `handle_text_input` / `handle_key` ignore a disabled element, so tying `disabled` to `can_execute()` would lock an empty field that the command itself requires to be non-empty. Return still executes only when `can_execute()` is true.

A hit on a **Checkbox** additionally flips its `checked` bool (before command dispatch, so a bound `command` still fires too) and writes it back through `checked="{binding}"` if bound — same `generated_owner`-aware target resolution (item VM inside an `ItemsControl` vs. the canvas `data_context`) as `drag`/`scroll-y` write-back. `KeyCode::Return` on the focused element mirrors this.

`begin_frame` resets `MouseConsumed` and reapplies FillWindow rects.

Gameplay **must** read `world.ctx<ui::MouseConsumed>().consumed_for(window)` before treating a click as a world pick.

## Keyboard editing & clipboard

`ui::handle_key`/`ui::handle_text_input` ([[src.ui.canvas.cpp]]) drive the focused element's
`caret_position`, UTF-8-aware (`prev_utf8_char`/`next_utf8_char`). Typing, `Backspace`, `Delete`,
`Ctrl+X` and `Ctrl+V` edit a focused `TextInput` only — a focused `Label` ignores them, so selecting
its text cannot rewrite it. `Left`/`Right`/`Home`/`End` move the caret on a `TextInput` and on a
focused selectable `Label` (the whole string, not the visual wrap row; there is no Ctrl+arrow word
move). `Escape` clears focus; `Return` toggles a focused Checkbox and/or executes a bound `command`.
No caret is drawn on a Label. Focusing one still sets `:focus`, so `Ctrl+C` has a target. A left
click that misses clears focus and the selection.

Ctrl/Shift are tracked in `world.ctx<ui::UiModifierState>()`, keyed by `WindowId`, updated by
`handle_key` special-casing `LCtrl`/`RCtrl`/`LShift`/`RShift` on both down **and** up — this stays
inside the SDL-free `ui` module rather than widening `KeyEvent`.

### Selection

`Element::selection_anchor` (`std::optional<std::size_t>`) is the fixed end of an in-progress
selection; `caret_position` is always the live end. A real (non-collapsed) selection is
`selection_anchor.has_value() && *selection_anchor != caret_position`, spanning
`[min(*anchor, caret), max(...))`. Set by:

- **Click** (`handle_pointer_impl`, left button) — places the caret at the clicked glyph and sets
  `anchor = caret = that index` (no selection yet, but armed for a drag). `MouseEvent::clicks >= 2`
  selects the word under the pointer (`word_range`, [[src.ui.text_select.cpp]]). `clicks >= 3`
  selects the whole string. A Label with `user-select: all` selects the whole string on the first
  click. Shift+click, when that same element is already focused, extends from the anchor it already
  has (`set_focus` clears the anchor, so the previous anchor is kept first); an unfocused
  Shift+click is an ordinary click.
- **Drag** — `ui::update_text_selection` (called from `run_input`'s `MouseEvent::Kind::Move`
  branch, [[src.ecs.systems.cpp]], whenever the button is still down) moves the live end. The
  gesture's granularity (character, word, or the whole string) lives in `UiTextSelectGestures`
  until the next pointer-down, per window, not on the Element. A word drag unions the seed range
  with the word under the pointer. A triple-click or `user-select: all` does not shrink. No
  re-hit-test: `UiFocusState` finds the element again each Move.
- **Ctrl+A** — `anchor = 0`, `caret = text.size()`. Works on a focused Label as well as a TextInput.
- **Shift+Left/Right/Home/End** — arms `anchor = caret_position` first if not already set, then
  moves the caret without collapsing.

A word is an approximation of UAX #29 with no Unicode library: Latin (including Latin-1 and
Latin Extended), Greek/Coptic, Cyrillic U+0400–U+052F, and ASCII digits. An apostrophe
(`'`, U+2019, U+02BC) or hyphen between two of those stays inside the word (`м'ясо`,
`well-known`). A run of spaces is its own range, a run of punctuation is its own, and every other
code point (CJK, emoji) is a one-character word.

`TextInput` still maps a click with `caret_index_for_click` (x only, prefix width added to the
`text-align` anchor). A selectable Label maps a click from `Element::painted_text_lines`, which
`paint.cpp` caches while it paints: per visual row, the UTF-8 byte range and the glyph box in
screen pixels (left, top, width, height). `text-align` and `align-items` are already in that left
edge, so a short centered row keeps its own glyph origin. Hit and highlight read only that cache.
A click is not compared to those boxes in raw window pixels. Ancestor scroll, and a Viewport
camera, are applied first — the same shift `hit_test` uses to enter the child — and the point is
scaled back into the cached box pixels. The highlight stays inside the paint-time pan, so it
already sits on the visible row. The element's own scroll is not part of that mapping; its text
is drawn before its own pan. A TextInput caret uses the same mapped x.
Vertical: the row under the pointer, or the nearest row when the pointer is between rows. Horizontal: the
nearest UTF-8 boundary, from `measure_text` of that row's substring. Characters between visual rows
(spaces the wrap dropped, and `\n`) are not highlighted, but they are part of the copied substring.
Before the first paint the index falls back to `text.size()`, same as a TextInput. The highlight is a
`selection-color` rect per box, drawn before the glyphs.

An inline formula is one atomic box on that cache (`split_inline` records the source span, including
the `\(` `\)` delimiters). A click in the left half of the box places the caret before the opener and
a click in the right half after the closer; drag, double-click, word-drag and Left/Right take the
whole span, so the copied substring is well-formed TeX. A word of the surrounding text stops at the
formula. `\\(` / `\\)` stay ordinary text; the painted box keeps a drawn-byte to source-byte map
because the escape is shorter on screen than in the source. A string with neither delimiter never
builds that split.

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
real text field). It copies a Label selection too, including a `\n` between visual rows. **Ctrl+X**
copies then erases, and **Ctrl+V** pastes at the caret (replacing the selection first), on a
`TextInput` only — on a Label neither changes the text, and Ctrl+X does not write the clipboard.
The OS clipboard itself is reached through
`world.ctx<ui::UiClipboard>()` — two `std::function`s (`set_text`/`get_text`), empty (no-op)
unless installed. `SdlGlPresentation::poll` installs the real SDL-backed pair once, lazily
([[src.render.opengl.clipboard.h]]/`.cpp`, since SDL's clipboard is process-global, not
per-window); `engine_tests` installs an in-memory fake per test
([[tests.ui_text_input_test.cpp]]) so clipboard behavior is covered without a real window.

`Element::allow_copy`/`allow_paste` (XML `allow-copy`/`allow-paste`, default both `true`, literal
only — no `{binding}`) lock clipboard access per-operation, mirroring how a web page blocks
copy/cut/paste independently by intercepting those DOM events rather than one combined on/off
switch. Both attributes parse on every element. `allow-copy="false"` blocks Ctrl+C on a Label the
same way it blocks a TextInput. `allow-paste` does not apply to a Label. Cut is gated by
`allow_copy` (it reads before deleting), not `allow_paste`.

## Files

- [[include.engine.ui.canvas.h]]
- [[src.ui.canvas.cpp]]
- [[src.ui.text_select.cpp]]
- [[src.ecs.systems.cpp]]
- [[src.render.opengl.clipboard.h]]
- [[tests.mvvm_test.cpp]]
- [[tests.ui_layout_hit_test.cpp]]
- [[tests.ui_text_input_test.cpp]]
- [[tests.ui_label_select_test.cpp]]

## See also

- [[features/UI Markup]]
- [[features/Input Mapper]]
- [[modules/UI]]
