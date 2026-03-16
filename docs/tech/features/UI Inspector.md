# UI inspector

The game turns it on with `ui::set_inspector_enabled(world, enabled)`. The engine does not bind a key. The overlay reads the live `Element` tree. It does not change layout, cascade, or the game's paint.

Header: `include/engine/ui/inspector.h`.

## Pick

`UiInspector::pick_pointer` starts true. The panel checkbox writes a view-model bool. The start of Bind copies that onto the flag, so the next click and this frame's paint both see it. Turning the inspector off sets the flag back on.

While the inspector is on and `pick_pointer` is set, a left click whose top canvas is not the inspector and not the profiler calls `hit_test_visual` instead of the interactive `hit_test`.

That walk matches paint order (z-index, scroll, Viewport camera, `display: none`, `visibility: hidden`, scrollbar track) and returns the deepest visible element, including a Label, Stack, or Image. `ItemTemplate` is skipped.

A hit writes `selection` for that window: canvas entity, path, and the nearest `generated_owner`. It inserts the window into `Presentation.mouse` and returns before focus, drag, and `execute()`. A miss does not select and does not consume. `:hover` and the wheel stay on the game path. Clicks on either tool panel take the normal path, so their buttons run. With `pick_pointer` off, a game click falls through. The hover box stops. The selection, the tree, and the detail stay.

The path is a child index per step. A step into `generated_items` sets `kGeneratedPathBit` (`src/ui/element_path.h`). No `Element*` is kept across frames. `generated_owner` finds a virtualized row after its index moves. If that row has left the window, the selection stays, the box disappears, and the tree row is not marked.

`hit_test_visual` also returns three rects in canvas space, after scroll and Viewport and before the canvas scale.

| Rect | Meaning |
| --- | --- |
| Border | `hit_bounds` (the AABB when rotated or scaled) |
| Margin | border expanded by margin |
| Content | border inset by padding. Border width does not shrink it |

## Panel

One window, title `"UI Inspector"`, 420×640, resizable. Its canvas is `FillWindow`, `order` 10000, tagged `InspectorPanel`. `inspector_skips_canvas` is true for that canvas and for `ProfilerPanel`.

The windowed presentation installs `ctx<InspectorWindowHost>()` and opens the window through `IWindowControl`. Headless tests have an empty host: the panel still gets a canvas and a `Presentation.sizes` entry, and no OS window is opened.

`begin_frame` opens the window if it is not up yet. Turning the inspector off destroys the canvas and closes that window. A close request for the inspector window does the same. The primary window is not closed. The close event is still delivered. The inspector reads it with its own cursor.

The tree lists every canvas that is not a tool panel, on every game window. Selection stays keyed by the game window. `detail_window` is the window of the last pick. The detail block, including `@media`, uses `window_size_for` for that window (`Presentation.sizes`). When more than one game window has a canvas, the root row is prefixed with the window id.

The document is `ui::Node` plus `parse_css`. No builtin GUID is added. Row sync runs at the start of Bind, before `run_bind`, and only while the inspector is on.

The tree is a flat `ItemsControl`: indent by depth, text `Kind #id .class`. Virtualization spacers and `display: none` stay visible. Expand state lives on the row view-model. A static node is keyed by its path. A generated row is keyed by `generated_owner`, so scrolling does not reset it. Clicking a row writes the same selection as clicking the game.

The selected block is read-only: kind, id, classes, text, pseudos, the three boxes, computed style from `style_cache_paint_` plus any running `motion_shown`, and the matched rules. Rules use the same match as `compute_style` (specificity, source order, `@media`, pseudos) for that element only. The inspector calls `match_style_rules` with `window_size_for` (`Presentation.sizes` for that window). Paint and layout call `compute_style` with the design size (`reference_size`) for a `ScaleWithScreenSize` canvas whose `reference_size` sides are both positive, so that canvas can show a different `@media` winner than the one that was painted. The last row is the winner. A `BindingId` is a hash, so a binding is shown as `text: bound` (and the same for the other bound fields), not as a path.

Style and box numbers in the panel are from the previous paint, because Bind runs before paint. The on-screen box is computed after this frame's layout. `wind-cli` reads the same fields after `draw_all`, so its numbers are this frame. See [CLI](CLI.md).

## Overlay

`CmdDrawUI` carries whether to draw hover, and the selection path when this canvas is the one selected. `run_ui_render` sets hover only on the top non-tool canvas under the pointer, and only while `pick_pointer` is set. A pointer over either tool window draws no hover.

`paint_document` strokes the boxes through `IUiPainter`. Hover and selection use different colors. When they are the same element, only the selection color is drawn. Neither tool canvas gets the tail. The boxes are inside that canvas's scissor, so `ScaleWithScreenSize` is already applied. There is no full-window overlay canvas.

After the boxes, one badge is painted in the same scissor. Its text is the tag (`Kind`, then `#id` when set, then `.class` for each class) and the border-box size in layout units. Whole numbers print as integers. Any other size prints to one decimal. The badge sits 4px above the border. When that would leave the canvas it sits below, and its x is clamped inside the canvas. While pick is on and the pointer is on a different element, the badge follows the hover. Otherwise it stays on the selection.

## Tests

`tests/ui_inspector_test.cpp`. No OS window.

## See also

- [UI Input](UI%20Input.md)
- [UI Profiler](UI%20Profiler.md)
- [CLI](CLI.md)
