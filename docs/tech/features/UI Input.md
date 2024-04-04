---
tags: [feature]
---

# UI Input

## Pointer

`run_input` copies `MouseEvent` into `UiPointer`, on Down calls `ui::handle_pointer` ([[src.ui.canvas.cpp]]).

## Hit-test

Canvases whose `rect` contains the point, sorted by `order` descending (higher = front). The top canvas is rebound and relaid out, then `find_button_at` looks for a **Button** under the cursor; only a hit Button sets `MouseConsumed` — a click over a Label/Stack/Canvas/Image with no Button does not consume it.

If `can_execute()`, `Execute()`.

`begin_frame` resets `MouseConsumed` and reapplies FillWindow rects.

Gameplay **must** read `world.ctx<ui::MouseConsumed>().value` before treating a click as a world pick.

## Files

- [[include.engine.ui.canvas.h]]
- [[src.ui.canvas.cpp]]
- [[src.ecs.systems.cpp]]
- [[tests.mvvm_test.cpp]]

## See also

- [[features/UI Markup]]
- [[features/Input Mapper]]
- [[modules/UI]]
