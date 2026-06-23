# UI inspector

The inspector has two halves. The engine keeps the probe: pick, the hover and selection boxes, path resolution, and snapshot functions that return plain data. The editor shows the panel: the Inspector tab of `wind_editor` ([Editor](Editor.md)). A game cannot open it. To inspect a game, Play it in the editor.

Header: `include/engine/ui/inspector.h`. Panel: `editor/src/inspector_panel.h`.

## Probe

`UiInspector` lives in the inspected world's `ctx`. `set_inspector_attached(world, true)` starts it with no selection and pick off. Attaching twice keeps the state. Detaching resets `UiInspector`: no selection, no collapsed rows, pick off. Nothing works on a world that is not attached.

The probe opens no window and spawns no canvas. Every canvas of the attached world is a game canvas.

| Function | Returns |
| --- | --- |
| `inspector_tree(world)` | One `InspectorTreeRow` per element, depth first, every canvas of the world |
| `inspector_select(world, window, pick)` | Selects a row's `pick` for that window and makes it the detail window |
| `inspector_toggle(world, key)` | Collapses an expanded row or expands a collapsed one. A leaf stays as it is |
| `inspector_selection(world, window)` | The `InspectorPick` of that window |
| `inspector_detail(world, pick)` | The computed block as text |
| `inspector_rules(world, pick)` | The matched rules, one string per rule |
| `inspector_hover_canvas(world, window)` | The top canvas under the pointer |
| `inspector_retarget(world)` | Moves each selection's path to where its element is now |

## Pick

`UiInspector::pick_pointer` starts off. The editor's Pick checkbox writes it.

While the world is attached and `pick_pointer` is set, a left click calls `hit_test_visual` instead of the interactive `hit_test`.

That walk matches paint order (z-index, scroll, Viewport camera, `display: none`, `visibility: hidden`, scrollbar track) and returns the deepest visible element, including a Label, Stack, or Image. `ItemTemplate` is skipped.

A hit calls `inspector_select` for that window: canvas entity, path, and the nearest `generated_owner`. It inserts the window into `Presentation.mouse` and returns before focus, drag, and `execute()`. A miss does not select and does not consume. `:hover` and the wheel stay on the game path. With `pick_pointer` off, a click reaches the game. The hover box stops. The selection stays.

The path is a child index per step. A step into `generated_items` sets `kGeneratedPathBit` (`src/ui/element_path.h`). No `Element*` is kept across frames. `generated_owner` finds a virtualized row after its index moves. `inspector_retarget` writes the new path. `run_ui_render` calls it while attached, before it builds the overlay commands, and `inspector_tree` calls it too. A selection whose canvas is gone turns inactive. If the row has left the window, the selection stays, the box disappears, and no tree row is marked.

`hit_test_visual` also returns three rects in canvas space, after scroll and Viewport and before the canvas scale.

| Rect | Meaning |
| --- | --- |
| Border | `hit_bounds` (the AABB when rotated or scaled) |
| Margin | border expanded by margin |
| Content | border inset by padding. Border width does not shrink it |

## Tree

`inspector_tree` walks every canvas with a live tree, ordered by window, then `order`, then entity index. Virtualization spacers and `display: none` elements are listed. A collapsed row's children are not.

| `InspectorTreeRow` field | Meaning |
| --- | --- |
| `key` | Canvas, `owner`, and `relative`. A static element is its path from the root. An element in a generated row is its path from that row, and `owner` is the row's `generated_owner` as a number. Scrolling a virtualized list keeps the key |
| `pick` | What `inspector_select` writes for this row |
| `window` | The canvas window |
| `tree` | `TreeRowInfo`: depth from the root, `has_children`, `expanded` (false on a leaf), and the parent row in this result ([UI](../modules/UI.md#trees)) |
| `label` | `Kind #id .class`, then ` spacer`, ` display:none`, or ` hidden`. When the world's canvases sit on more than one window, the root row starts with `[window] ` |
| `selected` | The row whose path equals that window's selection |

`inspector_tree` is `flatten_tree` over one root per canvas. Expansion lives in `UiInspector::expansion`, a `TreeExpansion` that starts every row expanded. Keys of a gone canvas are dropped on the next `inspector_tree`.

## Detail and rules

`inspector_detail` is read-only text: kind, id, classes, text (80 characters), pseudos, the three boxes, computed style from `style_cache_paint_` plus any running `motion_shown`, the bound fields, and the rule count. A `BindingId` is a hash, so a binding shows as `text: bound`, not as a path. With no active pick it is `Nothing selected`. When the element is gone it is `Selected element is not in the live tree.`

`inspector_rules` uses the same match as `compute_style` (specificity, source order, `@media`, pseudos) for that element only. It calls `match_style_rules` with `window_size_for` of the canvas window. Paint and layout call `compute_style` with the design size (`reference_size`) for a `ScaleWithScreenSize` canvas whose `reference_size` sides are both positive, so that canvas can show a different `@media` winner than the one that was painted. Each string is `selector (specificity)`, then one indented line per declaration. The last one, the winner, ends its first line with ` winner`.

Style and box numbers are from the last paint of the game world. `wind-cli` reads the same fields after `draw_all`, so its numbers are this frame. See [CLI](CLI.md).

## Overlay

`CmdDrawUI` carries whether to draw hover, and the selection path when this canvas is the one selected. `run_ui_render` sets hover only on the top canvas under the pointer, and only while attached with `pick_pointer` set.

`paint_document` strokes the boxes through `IUiPainter`. Hover and selection use different colors. When they are the same element, only the selection color is drawn. The boxes are inside that canvas's scissor, so `ScaleWithScreenSize` is already applied. There is no full-window overlay canvas.

After the boxes, one badge is painted in the same scissor. Its text is the tag (`Kind`, then `#id` when set, then `.class` for each class) and the border-box size in layout units. Whole numbers print as integers. Any other size prints to one decimal. The badge sits 4px above the border. When that would leave the canvas it sits below, and its x is clamped inside the canvas. While pick is on and the pointer is on a different element, the badge follows the hover. Otherwise it stays on the selection.

## Editor panel

The Inspector tab: a Pick checkbox and a hint, the tree on the left, Computed and Rules on the right (`editor/assets/ui/inspector.xml`, `editor/assets/css/panels.css`). See [Editor](Editor.md) for when it attaches.

`InspectorPanel::refresh` runs in the editor world's `Phase::Game`, before that world's Bind, while the tab is visible. It copies `inspector_tree` into `InspectorRowViewModel`s, reused by key so a row keeps its element, and copies the detail and the rules of the detail window's selection. A row binds its depth to `var-depth`; the row's left padding is `calc(var(--depth, 0) * 14px)`. The expander is a `Checkbox` drawn with `builtin::tree_chevron`, turned down while the row is expanded, and calls `inspector_toggle`; on a leaf it is disabled and draws nothing. The row button calls `inspector_select`. Pick is two-way: a checkbox click since the last refresh writes `pick_pointer`; otherwise the game's value is shown.

The tree has fixed 22px rows in a scrolling `ScrollView`, so it is virtualized.

While the Inspector tab is shown and the pointer is over the panel, the arrows, Home, and End move through the tree (`InspectorPanel::navigate`, `tree_navigate`): Up and Down move, Left collapses or goes to the parent, Right expands or goes to the first child. The start is the selected row of the detail window; with nothing selected, Down picks the first row. `EditorPanels` reads `KeyEvent` with its own cursor, repeats included, and calls `scroll_item_into_view` on `#tree` so the row stays visible. Keys are not `ActionId` bindings: the process has one binding table, which the game fills and Stop resets.

The panel inspects the world bound to `kPrimaryWindow`. A second world of the game (a tool window in its own world) is not in the tree.

## Tests

`tests/ui_inspector_test.cpp`: visual hit, paths and owner retarget, boxes, rule matching, attach and detach, pick and the command it skips, detail and rules text, tree rows, select, toggle (a leaf stays), generated rows that move, several windows, hover canvas, overlay, badge. `editor/tests/inspector_panel_test.cpp`: rows to view-models, depth and expanded, the row commands, tree keys, Pick both ways, detach. No OS window.

## See also

- [Editor](Editor.md)
- [UI Input](UI%20Input.md)
- [UI Profiler](UI%20Profiler.md)
- [CLI](CLI.md)
