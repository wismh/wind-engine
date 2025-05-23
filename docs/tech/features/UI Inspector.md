---
tags: [feature]
---

# UI Inspector

A runtime overlay the game turns on with `ui::set_inspector_enabled(world, bool)` ([[include.engine.ui.inspector.h]]). The engine does not bind a key. It reads the live `Element` tree and does not change layout, cascade, or paint.

## Pick

`hit_test` is unchanged: a Button, Checkbox, bound command or drag, camera Viewport, or selectable Label. `UiInspector::pick_pointer` starts on. The panel's Pick checkbox writes a view-model bool, and the start of Bind copies that onto the flag, so the next game click and this frame's paint both see it. Turning the inspector off sets the flag back on.

While the inspector is on and `pick_pointer` is set, a left click whose top canvas is not the inspector panel and not the profiler panel calls `hit_test_visual` instead. That walk is the same (z-index, scroll, Viewport camera, `display: none`, `visibility: hidden`, scrollbar track) and returns the deepest visible element, including a Label, Stack, or Image. `ItemTemplate` is skipped.

A hit writes `UiInspector::selection` for that window: canvas entity, element path, and the nearest `generated_owner`. It sets `MouseConsumed` and returns before focus, drag, and the game's command. A miss does not select and does not consume. `:hover` and the wheel stay on the game path, so a list under the pointer still scrolls. Clicks on the inspector panel or the profiler panel take the normal path, so their buttons run `ICommand`. With `pick_pointer` off, a game click falls through to focus, drag, and the game's command. The hover box stops. The selection box, the tree, and the detail stay.

The path is a child index per step. A step into `generated_items` sets `kGeneratedPathBit` (`src/ui/element_path.h`), the same encoding scrollbar drag uses. No `Element*` is kept across frames. `generated_owner` lets a virtualized row be found after its index moves. If that row has left the virtual window, the selection stays, the box disappears, and the tree row is not marked.

`hit_test_visual` also returns three rects in canvas space, after scroll and Viewport and before the canvas scale. Border is `hit_bounds` (the AABB when the element is rotated or scaled). Margin expands that by the margin. Content insets it by padding. Border width does not shrink the content box.

## Panel

One window, titled "UI Inspector", 420×640 and resizable. Its canvas is `FillWindow`, `order` 10000, tagged `InspectorPanel`. `inspector_skips_canvas` is true for that canvas and for `ProfilerPanel`, so the tree, the hover box, and pick skip both. The windowed presentation installs `InspectorWindowHost` and opens it through `IWindowControl`. Headless `engine_tests` have no host: the panel still gets a canvas and a `WindowSizes` entry, and no OS window is opened. `begin_frame` opens the window if it is not up yet. Turning the inspector off destroys the canvas and closes that window. A close request for the inspector window does the same. The primary window is never closed. The close event is still delivered to the game; the inspector reads it with its own cursor.

The tree lists every canvas that is not the inspector panel and not the profiler panel, on every game window. Selection stays keyed by the game window. `UiInspector::detail_window` is the window of the last pick, and the detail block (including `@media`) uses that window's size. When more than one game window has a canvas, the root row is prefixed with the window id. Highlight boxes stay on the game canvas.

The document is `ui::Node` plus `parse_css`. No new builtin GUID. Row sync runs at the start of `Phase::Bind`, before `run_bind`, and only while the inspector is on. The tree is a flat `ItemsControl`: indent by depth, text `Kind #id .class`, and virtualization spacers and `display: none` stay visible. Expand state lives on the row `ViewModel`. A static node is keyed by its path; a generated row is keyed by `generated_owner`, so scrolling the list does not reset it. Clicking a row writes the same selection as clicking the game.

The selected block is read-only: kind, id, classes, text, pseudos, the three boxes, computed style from `style_cache_paint_` plus any running `motion_shown`, and the matched rules. Rules use the same match as `compute_style` (specificity, source order, `@media`, pseudos) for that element only. The last row is the one that wins. A `BindingId` is a hash, so a binding is shown as `text: bound` (and the same for command, paint, and the other bound fields), not as a path. Style and box numbers in the panel are from the previous paint, because Bind runs before paint. The on-screen box is computed after this frame's layout. `wind-cli` reads those same fields after `draw_all`, so its style and boxes are this frame's paint. See [[features/CLI]].

## Overlay

`CmdDrawUI` carries an optional tail: whether to draw hover, and the selection path when this canvas is the one selected. `run_ui_render` sets hover only on the top canvas under the pointer that is not the inspector and not the profiler, and only while `pick_pointer` is set. The pointer in the inspector window or the profiler window draws no hover. `paint_document` calls `hit_test_visual` for hover and resolves the selection path, then strokes the boxes through `IUiPainter`. Hover and selection use different colors. When they are the same element, only the selection color is drawn.

After the boxes, one badge is painted in the same scissor. Its text is the tag (`Kind`, then `#id` when set, then `.class` for each class) and the border-box size in layout units, the same rect as the stroke. Whole numbers print as integers; any other size prints to one decimal. The badge sits 4px above the border. When that would leave the canvas it sits below, and its x is clamped inside the canvas. While pick is on and the pointer is on a different element, the badge follows the hover. Otherwise it stays on the selection. The same element draws one badge.

Neither panel gets the tail. The boxes are painted inside that canvas's scissor, so `ScaleWithScreenSize` is already applied. There is no full-window overlay canvas: `prepare_top_canvas` only considers the top canvas under the pointer and does not fall through a miss.
