# Docking

Panels tabbed together, split side by side, resized by splitters, dragged by their tab to another spot, and floated. Built in layers:

1. **Model**: `include/engine/ui/dock_layout.h`, `include/engine/ui/dock_geometry.h`. Plain data. No ECS, no rendering, no input, no fonts. Covered by `tests/dock_layout_test.cpp`.
2. **Host**: `include/engine/ui/dock_space.h`. A `DockSpace` component and two engine systems that draw the chrome, place panel canvases, and turn pointer gestures into model operations. Floats are virtual windows inside the space's window, or OS windows of their own (`DockFloatMode`). Covered by `tests/dock_space_test.cpp` and `tests/dock_float_window_test.cpp`. See [Host](#host) and [OS window floats](#os-window-floats).
3. **Editor**: `wind_editor`'s Project, Inspector, Profiler, and Build panels live in one `DockSpace` under the toolbar, with a default layout and a layout file. A floated panel gets an OS window ([Editor](Editor.md#panels)).

Each panel stays its own `UiCanvas` with its own document and view-model. The model only says where each panel goes. A host writes the rect, order, and window of each panel canvas from the geometry.

## Model

Namespace `engine::ui`. Sources: `src/ui/dock_layout.cpp` (tree and operations), `src/ui/dock_geometry.cpp` (rects), `src/ui/dock_hit.cpp` (chrome and drop hit tests), `src/ui/dock_text.cpp` (TOML), `src/ui/dock_reconcile.cpp`.

A `DockLayout` is one docked tree (the root may be empty) and a list of floats in z order, last on top. A panel is a stable string key (`"project"`, `"inspector"`), so a saved layout survives a rebuild.

| Type | What it is |
| --- | --- |
| `DockNode` | `Tabs`: ordered `panels` and the `active` index. `Split`: `axis`, `ratio`, `first`, `second`. `parent` is `kNoDockNode` on a tree root |
| `DockAxis` | `Horizontal` puts `first` left of `second`; `Vertical` puts it on top |
| `DockFloat` | `id`, its own subtree `root` (tabs or splits), and the frame `rect` |
| `DockTarget` | `node` plus `DockZone`. `node == kNoDockNode` is the dock area: an edge splits the docked root; Center fills an empty root or joins a `Tabs` root |
| `DockZone` | `Center` joins a tab stack. `Left`, `Right`, `Top`, `Bottom` split the target and put the panel on that side at ratio 0.5 |
| `DockPanelPlace` | What `find` returns: the stack, the tab index, the float (`kNoDockNode` when docked) |

Node and float ids come from one counter (`next_id`), never collide, and are never reused by the layout. A node keeps its id while it exists.

| Operation | Does |
| --- | --- |
| `add(key, target)` | New panel, active. False for an empty or known key, an unknown node, or Center on a split |
| `remove(key)` | The neighbour tab becomes active. An empty stack goes; its split collapses into the other child; an empty float goes |
| `move(key, target)` | From anywhere, a float included. False when it is no change: Center on its own stack, or any zone of its own stack when the panel is alone there. The dock area counts as its root. A target split that collapses when the panel leaves is replaced by its surviving child |
| `float_panel(key, rect)` | Into a new float on top. A panel alone in a float only gets the rect and is raised |
| `dock_float(id, target)` | The whole float, outside itself. Center appends its tabs (its root must be a stack, its active tab stays active); an edge inserts its subtree |
| `set_float_rect`, `raise_float` | Move or resize; put on top |
| `activate(key)`, `reorder(key, index)` | Index clamped to the last tab. The active panel stays active |
| `set_ratio(split, ratio)` | Clamped to `[kDockRatioMin, kDockRatioMax]` (0.01, 0.99). False on NaN or a non-split |
| `find`, `contains`, `is_visible`, `panels`, `float_of`, `stacks_under` | Queries. `is_visible` is the active tab of its stack |

Every operation leaves `valid()` true and changes nothing when it returns false: keys unique and non-empty, no empty stack, every split has two children, parents match, each node under exactly one root, no empty float, active index and ratio in range.

## Geometry

`compute_dock_geometry(layout, area, metrics)`. The docked tree fills `area`. Floats sit at their stored rects, in the same space.

`DockMetrics` is host-supplied: `tab_strip_height` (24), `splitter_thickness` (4), `title_bar_height` (22), `frame_border` (4), `min_panel_size` (48), `tab_width` (120) or `tab_width_for(key)` (no font access here; the dock host measures titles, [Tab width](#tab-width)), `drop_edge_share` (0.25), `root_edge_band` (24 px), `float_size` (320 × 240).

| Output | Rects |
| --- | --- |
| `DockPanelRect` | Stack content and `shown`. An inactive tab has the content rect and `shown` false: the host makes it `Fixed` with an empty rect |
| `DockStackRect` | Whole rect, tab `strip` (top), `content` below, `tabs` left to right. Tabs that overflow the strip shrink in proportion |
| `DockSplitterRect` | `grab` bar between the children and the split's whole `area` |
| `DockFloatRect` | `frame` (the stored rect), `title` inside the border, `content` under the title. Bottom to top |

A split's length minus the bar is shared by `ratio`; each side gets at least `min_panel_size` when both fit, otherwise the ratio holds (`dock_split_rects`).

Helpers: `dock_split_ratio_at(splitter, point, metrics)` puts the splitter's middle under the pointer, both sides at least `min_panel_size` (0.5 when they do not both fit). `dock_float_resized(start, edges, delta, metrics)` keeps the content at least `min_panel_size`; a dragged left or top edge stops, the opposite edge stays. `dock_float_clamped(rect, area)` moves a float inside the area; one larger than the area aligns left or top.

## Hit tests

`dock_chrome_at(geometry, metrics, point)` returns a `DockChromeHit`: `Tab` (stack and key), `Splitter`, `FloatTitle`, or `FloatEdge` with `DockResizeEdges` (a corner sets two; the band is `frame_border`). Floats are tested top first and cover what is under them: a point on float content hits nothing. `dock_float_at` is the topmost float under the point, for raise on click.

`dock_drop_for_panel(layout, geometry, metrics, point, key, grab)` while a tab is dragged:

1. Floats, top first. A float that holds only `key` is skipped (it moves with the pointer). A point on a float frame but over no stack (the title) is nothing.
2. Outside the dock area: `Float`, frame `float_size` placed so the pointer sits at `grab` inside it. Apply with `float_panel(key, preview)`.
3. An empty docked root: Center of the dock area, preview the whole area.
4. A docked tab strip: Center of that stack, even inside the root band.
5. The root edge band (`root_edge_band` px): that edge of the dock area.
6. The stack under the point: the nearest edge whose distance is under `drop_edge_share` of the stack's width or height, else Center. A splitter is nothing.

The preview is what the drop makes: the half a 0.5 split gives, or the whole stack for Center. A drop that changes nothing is nullopt. Apply `Dock` with `move(key, target)`.

`dock_drop_for_float(layout, geometry, metrics, point, float_id)` while a float is dragged by its title: the same, with that float skipped, no `Float` result, and Center only when the float's root is a stack. Apply with `dock_float`.

## Text

`dock_layout_to_text` writes TOML (`version = 1`, `next_id`, `root`, `[[node]]`, `[[float]]` with `rect = [x, y, w, h]`). A float is written in the shortest form that reads back to the same bits. `dock_layout_from_text(dock_layout_to_text(x)) == x`, ids included. Parse errors, wrong types, an unknown version or kind, and a layout that breaks an invariant are nullopt. Parents are not stored; they follow from the splits. tomlplusplus stays in `src/`.

## Reconcile

`reconcile_dock_layout(layout, registered, spot)` after loading: drops every panel not registered, then adds each missing registered panel, in order, at `DockSpot` (`beside` a panel with a zone, or the zone of the dock area when `beside` is absent). Center on a split root falls back to the first docked stack. Returns true when anything changed.

## Host

A `DockSpace` component on an entity the host creates. Namespace `engine::ui`. The engine registers its systems with the UI systems, so any world with `register_engine_systems` runs them: games and the editor alike.

| `DockSpace` field | Role |
| --- | --- |
| `window` | The window the space, its chrome, and its panels draw in |
| `area` | Window pixels the docked tree fills. Floats are kept inside it. The host keeps it current |
| `metrics` | `DockMetrics`. `tab_width_for` empty: the dock system measures each title ([Tab width](#tab-width)) |
| `layout` | The `DockLayout`. The host may change it any time; the next frame shows it |
| `panels` | `DockPanel`: `key`, `title` (empty shows the key), `canvas` (the host's `UiCanvas` entity), `closable` |
| `float_mode` | `DockFloatMode::Virtual` (default): a float is a virtual window inside `area`. `OsWindow`: an OS window per float ([OS window floats](#os-window-floats)). A change converts the floats on the next frame |
| `order` | Lowest canvas order the space uses |
| `stylesheets` | Chrome sheets, merged in order. Default `{builtin::dock_css}` |
| `drag_threshold` | Pointer travel (4 px) that turns a tab press into a drag |
| `close_button_size` | 14 px |
| `revision` | Bumped when the dock system changes `layout`, an OS float window's native move, resize, or close included. Host edits do not bump it |

```cpp
const engine::ecs::Entity dock = world.create();
engine::ui::DockSpace& space = world.emplace<engine::ui::DockSpace>(dock);
space.window = window;
space.area = {0.0f, toolbar_height, width, height - toolbar_height};
space.order = 10;
space.panels = {{"project", "Project", project_canvas}, {"inspector", "Inspector", inspector_canvas, true}};
space.layout = engine::ui::dock_layout_from_text(saved).value_or(default_layout());
engine::ui::reconcile_dock_layout(space.layout, keys, {.zone = engine::ui::DockZone::Right});
// Each frame (Game phase): space.area on resize; save when space.revision != saved_revision; read
// DockPanelCloseRequested and remove the panel from space.layout (and its canvas) when it agrees.
```

### Systems

| Phase | System | Does |
| --- | --- | --- |
| `Input`, after `run_input` | `run_dock_input` (`src/ui/dock_space_input.cpp`) | Reads `MouseEvent`, `KeyEvent`, and `WindowCloseRequestedEvent` with its own cursors. Presses on chrome, gestures, `MouseConsumed`, an OS float window's close button |
| `Bind`, before `run_bind` | `run_dock_layout` (`src/ui/dock_space_layout.cpp`) | Opens, follows, and closes OS float windows (`src/ui/dock_float_windows.cpp`), measures tabs, then geometry onto panel and chrome canvases; spawns and destroys chrome |

Both get the `EngineSystemDeps` of `register_ui_systems`. `windows` (`IWindowControl`) and `worlds` (`Worlds`) are what OS window floats need; `EngineHost` sets both. Either unset (headless tests, `Host`): floats stay virtual.

Input runs after `run_input`, so a press is judged against the canvases that were drawn. Layout runs after the Game phase, so a host change shows in the same frame.

### Canvases and z order

Every registered panel canvas gets `fit = Fixed`, `window`, `rect`, and `order` each frame. A shown panel gets its stack's content rect. An inactive tab, a panel the layout lacks, and every panel while `area` is empty get an empty rect, so they take no clicks. A panel that stops being shown loses its open popups and keyboard focus (`release_canvas`, `src/ui/popup.h`). A key registered twice: the first wins.

| Order | Canvas | Rect |
| --- | --- | --- |
| `order` | Docked chrome: stack backgrounds, strips, tabs, splitters | `area` |
| `order + 1` | Docked panels, and hidden ones | Content |
| `order + 2 + 2i` | Chrome of float `i` (0 is the bottom): frame, title, its stacks and splitters | Frame |
| `order + 3 + 2i` | Panels of float `i` | Content |
| `order + 2 + 2n` | Drag preview | The drop preview; empty when nothing is dragged |

`dock_space_order_count(layout)` is `3 + 2n`. A float's chrome is below its own panels and above everything of a lower float. Floats are opaque: the topmost canvas under a point of a float is that float's chrome or panel. A float in an OS window keeps its orders, in its own window; the preview canvas moves to the window the drop is previewed in.

The chrome canvases are engine entities (`world.ctx<DockRuntime>()`, `src/ui/dock_runtime.h`), one per layer, floats keyed by float id. Removing the `DockSpace` or its entity destroys them on the next layout pass. Each is a `ui::Node` document (`build_dock_chrome_document`, `src/ui/dock_chrome.cpp`) with one `ItemsControl` per box kind, bound to a `DockChromeViewModel`. The rows are `position: absolute`: a list hugs nothing of them (0 × 0), paint draws a row outside its list's box, and a hit test reaches it there ([UI Input](UI%20Input.md#what-a-hit-is)), so every tab takes hover and `wind-cli hit` answers the tab, wherever the geometry puts it. A row (`DockChromeItem`) carries `x`, `y`, `w`, `h` relative to the canvas, `title`, `active`, the close button, and `reserve` (`--reserve` on the tab). Rows are reused, so only changed values re-lay out.

### Theme

`builtin_assets/css/dock.css` (`builtin::dock_css`):

| Class | Element |
| --- | --- |
| `.dock-box` | Every placed row: `position: absolute` from `--x`, `--y`, `--w`, `--h`. Keep it |
| `.dock-stack`, `.dock-strip` | Stack background, tab strip (`Stack`) |
| `.dock-tab-item` | A tab's row (`Stack`). Keep padding 0: the close button is placed inside it |
| `.dock-tab` | The tab (`Button`, `content` is the title). `:checked` is the active tab. Its font, size, and left and right padding set the tab's width ([Tab width](#tab-width)). `--reserve` is the room a closable tab's close button takes on its right (0 otherwise); the default pads the right with `calc(8px + var(--reserve, 0px))` so the title stays clear of the button |
| `.dock-tab-close` | Close `Button` (`×`) from `--cx`, `--cy`, `--cs`; `display` from `--close` (`none` or `block`) |
| `.dock-splitter` | Splitter bar (`Button`, so `:hover` and `:pressed`) |
| `.dock-frame`, `.dock-title` | Float frame (`Stack`) and title bar (`Button`, the active panel of the float's first stack) |
| `.dock-preview` | Drop preview |

A host restyles with its own sheets in `stylesheets`. Positions and sizes come from the geometry, not from CSS.

### Tab width

`measure_dock_tabs` (`src/ui/dock_tab_measure.cpp`), at the start of `run_dock_layout`, fills `DockSpaceRuntime::tabs` with one width per registered panel: `ceil(text + padding-left + padding-right)`. The text is the panel's title measured by the space window's layout painter (`layout_painter_for`, the one hit-test layout uses). Font, size, and padding come from the stylesheet the docked chrome canvas actually loaded (`UiInstance::stylesheet`, the merged `stylesheets`), resolved on a copy of the chrome's tab row (`build_dock_tab_probe`: the canvas, the tabs `ItemsControl`, the row, the `.dock-tab` button) with `apply_layout_style`, so a theme that changes the tab font or padding changes the width too. A closable tab is resolved with `--reserve` set to `dock_close_reserve(tab_strip_height, close_button_size)`: the button plus its gap to the tab's right edge (19 px by default). Widths are cached per key with the title and `closable`, and measured again when the painter, the stylesheet (pointer or generation), the reserve, or the window size changes.

`dock_space_metrics(space, runtime)` is the space's `metrics` with `tab_width_for` reading those widths (a key without one is `tab_width`). `dock_space_geometry(space, runtime)` uses it, so input and layout see the same tabs. A host `tab_width_for` wins. No painter (headless, `engine_tests` without a fake) or no loaded chrome sheet yet (the first frame) clears the widths: every tab is `tab_width` until both exist. Tabs that do not fit the strip still shrink in proportion.

### Input

Per mouse event, spaces are visited from the highest `order`. Only events of the space's `window` and of its float windows count. A press goes to one space at most. Each event is judged against the geometry of the window it came from: `dock_space_geometry` for the space's window (docked tree and virtual floats), `dock_float_window_geometry` for a float window (that float's tree in its client area, no frame or title bar).

Chrome is a docked tab strip, a docked splitter, or anywhere inside a virtual float frame (content included: a float is a window). In a float's OS window, chrome is its tab strips and splitters. Panel content is not chrome; the panel's own UI decides. A point is blocked when an open popup of that window is under it (`popup_canvas_at`), or when a canvas the space does not own, in that window, with an order above the chrome's layer, holds it. Chrome that is not blocked consumes Down, Move, and Wheel (`Presentation.mouse`), so gameplay under it ignores them. A gesture consumes every Move.

| Press on | Does |
| --- | --- |
| Any virtual float (any button) | `raise_float` when it is not on top. An OS window's stacking is the OS's |
| A tab | `activate` when inactive, then a tab press. On a closable tab's close button: `DockPanelCloseRequested{space, key}` instead; the host removes the panel or not |
| A splitter | Splitter drag |
| A float title | Float move |
| A float edge or corner | Float resize |

Rows after the first are the left button. Another button on chrome is consumed and does nothing else.

| Gesture | Move | Release |
| --- | --- | --- |
| Tab press | Past `drag_threshold`: a tab drag. A panel alone in a virtual float drags the float (float move) | Nothing more |
| Tab drag | `dock_drop_for_panel` on the preview canvas. A `Float` drop is moved to the float home. Shift held: a float at the pointer. OS window floats: [across windows](#dragging-across-windows) | `move` or `float_panel` |
| Splitter | `set_ratio(dock_split_ratio_at)` live | Commit |
| Float move | `set_float_rect` live at the float home; `dock_drop_for_float` preview, none with Shift | `dock_float` on a drop, else keep the rect |
| Float resize | `dock_float_resized`, the pointer clamped into `area` | Commit |

Escape (key down in one of the space's windows), another button pressed, or the button found up without an Up (`pointer_for` of the press's window, `down` false after the frame's events) cancels: the ratio or float rect goes back, nothing docks. `revision` is bumped once per activate, raise, and committed gesture that changed the layout.

With virtual floats the whole area is dock targets, so a tab is floated by a drop outside `area` or with Shift.

### Float home

`dock_float_home(space, runtime, float_id, frame)` is the one place that says where a float lives (`DockFloatHome`: window, rect, `os_window`). A float the runtime holds an OS window for lives in that window, its tree filling the client area from the origin (`{0, 0, w, h}`). Any other float lives in the space's window at `dock_float_clamped(frame, area)`. `dock_space_geometry(space, runtime)` shows virtual floats at their homes without changing the stored rect, so a float outside a shrunk area is shown inside it and goes back when the area grows, and leaves OS window floats out. Float drops, moves, and the float canvases all go through it.

## OS window floats

`DockFloatMode::OsWindow`. Each float is an OS window bound to the space's world, a tool window of the space's window: `WindowDesc::owner` is `DockSpace::window` and `WindowStyle::utility` is set, so it stays above that window, hides and minimizes with it, and has no taskbar entry ([Windowing](Windowing.md#contract)). The OS title bar and frame replace the virtual ones: the float's tab strips, splitters, and panels fill the client area, its panel canvases get `UiCanvas::window` of that window, and its chrome canvas is `{0, 0, w, h}` there with no frame or title rows. The stored `DockFloat::rect` is the client rect in the space window's client pixels: screen position minus `IWindowControl::position(space.window)`. A mode switch keeps the rects: a virtual frame becomes a client rect and back.

`sync_dock_float_windows` (`src/ui/dock_float_windows.cpp`), at the start of each layout pass:

| Case | Does |
| --- | --- |
| A float without a window | `open_window`: title the active panel of its first stack, size the rect, position the space window's position plus the rect, owner the space's window, `utility`. A rect whose top 24 px row is on no usable display (`usable_display_bounds_for_window` of the space's window and `usable_display_bounds(0..7)`) is moved onto the space's display, and the layout takes that rect (`revision` bumped). Then `Worlds::bind_window` to the space's world |
| A window's position or size differs from the last applied or read | A native move or resize: the rect follows, `revision` bumped |
| The rect differs from the last applied | A drop or a host change: `set_position` and `resize` |
| The title changed | `set_title` |
| The float went (docked, emptied, removed) | `unbind_window`, `close_window` |
| A window something else closed | Forgotten; a new one opens |
| `Virtual` mode, or no `windows`/`worlds` in the deps | Every window of the space closes; its floats are virtual |
| `open_window` fails | Every window of the space closes and its floats stay virtual until `float_mode` changes (logged). Also when the space's window was closed: closing it closed the float windows it owned, and a closed window cannot own a new one |

Removing the `DockSpace` (or its entity) closes its windows on the next layout pass. Destroying the world closes them at once: `Worlds::destroy` calls `close_world_dock_float_windows` (unbind, `close_window`) for a world that draws UI, before it drops the world. Otherwise they would stay open unbound, and their command buffers' `CmdDrawUI` from the last frame would point into the dead world's documents. `Worlds::destroy` runs outside `draw_all` (a Game-phase command, the editor's Stop), so the same rule as the layout pass holds: `destroy_window` drops the buffer with the window.

`dock_panel_os_window(world, space, key)` is the OS window a panel is shown in (its float's, once opened); nullopt for a docked panel, a virtual float, or an unknown key. `raise_float` orders floats in the layout only; a host that brings a panel to the front raises that window too (`IWindowControl::raise`, the editor's `EditorPanels::show`).

The close button of a float window (`WindowCloseRequestedEvent`, read in `run_dock_input`) docks the float back and bumps `revision`; the layout pass then closes the window (`dock_float_window_closed`). The spot: a float that is one tab stack joins the first docked stack (depth first; its active tab stays active), a float with splits goes to the right edge of the dock area (a 0.5 split), and an empty docked tree takes it whole. Hiding instead of re-docking would need a way to bring a panel back; the host can still remove the panel from the layout itself.

### Opening and closing in the frame

Windows open and close in `run_dock_layout`, in `Bind`, never inside `draw_all`. A window opened there gets this frame's `CmdDrawUI` from `run_ui_render` (`ensure_ui_font` loads `builtin::font_ui` into its NanoVG context first). `WindowManager::destroy_window` drops the window, its GL context, and its command buffer at once; that buffer's `CmdDrawUI` entries from the last frame point into documents, but nothing reads the buffer after it is gone, and the same pass has already moved the float's canvases to the space's window, so no canvas of this frame targets the closed id (`run_ui_render` skips a window without a buffer). The modal move loop's reentrant tick runs the same systems; a close there would only come from the host removing the space.

### Dragging across windows

While a button is down, SDL auto-capture (`SDL_HINT_MOUSE_AUTO_CAPTURE`, on by default) keeps sending motion and the release to the window that got the press, in its client pixels even outside it. The gesture keeps that window (`DockGesture::window`), and a tab drag turns the pointer into screen pixels with `IWindowControl::position` of that window:

1. Shift: a new float with the pointer at `grab` (the press point inside the tab; no frame or title offset), `float_size`.
2. The window under the screen point among the space's: its float windows top of the layout's z order first, then its own window. Other applications' windows are not known: one above the space's window does not hide it.
3. The space's window: `dock_drop_for_panel` on its geometry. `Dock` previews there; `Float` (outside `area`, the toolbar) is a new float at the pointer.
4. A float window: `dock_drop_for_panel` on that window's geometry; the preview is drawn in that window. A panel alone in that float drops nothing there.
5. None: a new float at the pointer.

A new float has no preview (its window opens on release, at the pointer). Release applies `move` or `float_panel` as before; a float that empties loses its window in the same frame's layout pass. Dragging the only tab of a float window moves that panel: onto a dock target it docks and the window closes, elsewhere `float_panel` moves the window under the pointer. The window itself is moved and resized by its OS title bar and frame. A splitter drag stays in its window.

Screen and client pixels are taken as the same unit (true on Windows, where SDL's window coordinates are pixels). Without `position` for a window the drag is judged in the press's window only.

### Tests

`tests/dock_float_window_test.cpp` runs a `Worlds` world with the fake `IWindowControl` (`tests/fixtures/fake_services.h`, which keeps each window's description, position, size, and title): a float's window bound to the world with its title, size, and position and its panels and chrome in it; a mode switch both ways; a tab dropped outside the window and a Shift drag opening a window at the pointer; a tab dragged from a float window docking in the space's window with the preview there, and the last tab out closing the window; a tab joining another float window's stack; a splitter in a float window; panel content left to the panel; Escape in the drag's window; native move and resize into the rect and a rect change back to the window; the close button re-docking (one stack and with splits); removing the space; destroying the world (its float windows close and unbind, the world's own window stays; another world's destroy leaves them); the space's window closed (its owned float windows close with it and the floats turn virtual); owner and `utility` of a float window and `dock_panel_os_window`; a window that does not open; a saved float reopening at its rect; a float off every display; no window control. Synthetic events only: real SDL capture across windows is not in `engine_tests`.

## Next

- Tabs are driven by `MouseEvent` on the chrome, not by `ICommand`: `wind-cli click` on a `.dock-tab` answers `no command`. `wind-cli dock` switches tabs, moves and floats panels, and switches the float mode through `DockLayout` instead ([CLI](CLI.md#dock)).
- Clicking a float window raises it by the OS; that does not reorder the layout's floats (`raise_float`), so the saved z order is the last one the dock system or host set.
- Where SDL cannot set an owner (`SDL_SetWindowParent` unsupported) a float window opens unowned: a top-level window that can fall behind the space's window.
- A drop between windows ignores other applications' windows above the space's windows.
- A panel that needs a render target of its own (the game view as a dock tab) is render-to-texture, a separate task.

## See also

- [UI](../modules/UI.md)
- [UI Input](UI%20Input.md)
- [Windowing](Windowing.md)
