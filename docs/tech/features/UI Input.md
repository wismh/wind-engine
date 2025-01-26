---
tags: [feature]
---

# UI Input

## Pointer

`run_input` copies `MouseEvent` into `UiPointer`, on Down calls `ui::handle_pointer` ([[src.ui.canvas.cpp]]).

## Hit-test

Canvases whose `rect` contains the point, sorted by `order` descending (higher = front). The top canvas is rebound and relaid out, then `hit_test` ([[src.ui.document.cpp]], shared with `paint.cpp`'s `:hover` resolution) looks for a **Button**, or any element with a bound `command` or `drag`; a plain Label/Stack/Image does not consume.

That relayout uses the same per-window `IUiPainter` as paint (`world.ctx<UiLayoutPainters>()`, set by `EngineRuntime` from each window's NanoVG painter after `MakeCurrent`). Hug text then matches glyphs. No painter registered (headless `engine_tests`, a window created without a UI painter) keeps the CPU fallback in [[src.ui.document.cpp]].

Within a canvas, `hit_test` visits siblings topmost-first — the reverse of `child_stacking_order()` (z-index ascending, document-order tie-break), so it always agrees with paint order: the element drawn on top is also the one that receives the click. Its containment check uses `hit_bounds()`, not the raw `layout_rect` — for a rotated/scaled element (`transform: rotate() scale()`) that's the axis-aligned bounding box of the transformed corners, an approximation (slightly generous at a rotated element's corners), not a precise oriented-rect test.

If `can_execute()`, `Execute()`.

`begin_frame` resets `MouseConsumed` and reapplies FillWindow rects.

Gameplay **must** read `world.ctx<ui::MouseConsumed>().consumed_for(window)` before treating a click as a world pick.

## Files

- [[include.engine.ui.canvas.h]]
- [[src.ui.canvas.cpp]]
- [[src.ecs.systems.cpp]]
- [[tests.mvvm_test.cpp]]
- [[tests.ui_layout_hit_test.cpp]]

## See also

- [[features/UI Markup]]
- [[features/Input Mapper]]
- [[modules/UI]]
