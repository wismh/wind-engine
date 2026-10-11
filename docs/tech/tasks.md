# Engine tasks from td-over limits

Source: `td-over/docs/engine-limits.md` (E-numbers below refer to it), measured against SDK 0.1.5a. Each task is a
branch `feat/wind-N-short-kebab` from `main`. Numbering continues after `wind-212`; renumber if another task takes the
number first. E17 is td-over's own debt (tests/ctest) and is not an engine task.

Check each E-item against `include/engine/` before starting: as of this writing none of `bind_to`, a change
notification on `Bindable`, a cursor/capture API, a Slider control, or a RNG exists in the public headers.

## Order

`wind-213` and `wind-214` unblock most of td-over's boilerplate; do them first. `wind-215` needs `wind-213`
(typed bindings). `wind-216`, `wind-217`, `wind-219`, `wind-220` are independent. `wind-218` is independent of UI.

---

## wind-213 feat: bindable change notification, visibility and typed length bindings (E1, E3, E4)

Problem: a value the XML changes itself (drag, TextInput, Checkbox) can only be read by polling; show/hide needs a
0-or-1-item `ItemsControl` or a `"none"`/`"flex"` string; positions are formatted as `"12.5px"` strings every frame.

Do:
- `Bindable<T>::on_changed(std::function<void(const T&)>)` (or a dirty flag cleared after `Presentation`), fired for
  writes from XML as well as from C++.
- `visible="{binding is_open}"` on any element, fed by `Bindable<bool>`; optionally `<If condition="...">`.
- Typed length binding: `Bindable<float>` / `Bindable<ui::Length>` usable in `var-left="{binding x}"` etc., pixels by
  default, no string formatting.

Done when: tests in `engine_tests` cover XML-originated change notification, visibility toggle without layout
residue, and a float-bound `left`; `docs/tech/features/UI Markup.md` and `docs/manual` updated; no string-formatting
path left as the only way to move an element.

## wind-214 feat: `RelayCommand::bind_to` and late-owner commands (E2)

Problem: `RelayCommand` takes only `std::function`; each button costs a lambda plus a public hook in the view model.
`MemberCommand<Owner,&Method>` could not work because generated `Asset::bind()` needs the view model before its owner
exists.

Do:
- `RelayCommand::bind_to<Owner, &Owner::method>(Owner&)`, as the `wind-cpp` skill already documents.
- A way to assign the owner after construction (so a typed command works with generated `bind()`), or document that
  `bind_to` replaces it.

Done when: tests cover both forms and re-binding to a new owner; the `wind-cpp` skill and manual match the real API.

## wind-215 feat: absolute-positioned, drag-bound `ItemsControl` items and a Slider control (E5, E9)

RFC: td-over `docs/engine-proposals/drag-binding-in-itemscontrol.md`. Needs wind-213.

Problem: `ItemTemplate` cannot take absolute `var-left/var-top` per item or `drag="{binding}"`, so every inventory item
is hand-written in five places. There is no Slider, and CSS `transform` has no `translate()`, so slider handles are
positioned by duplicated C++ and CSS constants.

Do:
- Per-item absolute position bindings and `drag="{binding}"` inside `ItemTemplate`.
- `<Slider value="{binding fraction}" min max step/>`; add `translate()` to the CSS `transform` subset.

Done when: tests cover drag inside a templated item and slider value round-trip; `UI Markup.md` lists the new tag and
function.

## wind-216 feat: global cursor, mouse capture and pointer targets for sprites (E6, E7)

Problem: dragging across or out of secondary windows needs Win32 `GetCursorPos`/`GetAsyncKeyState` in game code;
click-through over sprites needs hand-rolled hit tests and writes into `mouse.consumed_windows`, with a hidden
read-before-write ordering.

Do:
- `IWindowControl::cursor_screen_position()`, `is_mouse_button_down(MouseButton)`, `capture_mouse(WindowId)` (or
  deliver mouse events to the owning window while a button is held).
- A `PointerTarget`/`Clickable` component (rect or derived from `Sprite`) that the input system itself counts for
  click-through, delivering hover and click without game-side `consumed_windows` bookkeeping.

Done when: headless tests cover hit-testing and capture state; `Windowing.md` and `UI Input.md` updated; a game needs
no `windows.h` for drag or click-through.

## wind-217 feat: RAII `UiWindow` for secondary windows (E8)

Problem: open window, bind world, create canvas, set drag region (excluding the close button), then the mirror
sequence on close, repeated per window; close requests are dispatched by an `if-else` chain over `WindowId`.

Do: `UiWindow::open(world, window_desc, canvas_desc)`, destructor tears down canvas, bind and window;
`on_close_requested` callback in the descriptor; a drag-region option that excludes UI elements; a centre-on-work-area
helper.

Done when: tests cover open/close/destroy ordering without a real window; `Windowing.md` updated.

## wind-218 feat: per-instance material parameters and `Sprite` from animation frame (E10, E15)

RFC: td-over `docs/engine-proposals/per-instance-shader-overrides.md`.

Problem: a hit flash needs a tag, a per-frame system, a dedicated material and a component; spawning from an
animation clip copies seven `Sprite` fields by hand.

Do: per-instance material parameter overrides (set once, no per-frame rewrite); `Sprite::from_frame(const
SpriteFrame&)` or first-frame auto-init in `SpriteAnimator`.

Done when: tests cover override isolation between two instances of one material and frame-to-sprite initialization;
`Materials and Sort.md` updated.

## wind-219 feat: public config read/write and `engine::Random` (E13, E14)

Problem: games hand-parse JSON-ish files because tomlplusplus is private; every game keeps its own `mt19937`, so
tests and replays are not deterministic.

Do: `engine::config` (key-value or table, over `user_data_directory`); `engine::Random` in `ctx<>` seedable at start.

Done when: tests cover round-trip and malformed input (error, not UB); same seed gives the same sequence; no
toml/random third-party header leaks into `include/`.

Status: in progress (wind-219).

### Migration (wind-219, E13, E14)

- Old: a game-owned `std::mt19937` (per system or per game) and a hand-parsed JSON-ish settings file.
- New, random: `world().ctx<engine::Random>()` (`<engine/core/random.h>`): `uniform(int, int)` (inclusive),
  `uniform(float, float)`, `next_float()`, `chance(p)`, `pick(range)`, `next_u32()`. Fixed seed for tests and replays:
  override `IGame::random_seed()` or call `ctx<engine::Random>().seed(n)`. The same seed gives the same numbers on every
  platform, so a replay or a test can pin golden values. The default is a clock seed.
- New, settings: `engine::Config::load(path)` / `save(path)` (`<engine/core/config.h>`) with `get_int("audio.volume", 80)`,
  `set_int(...)`, `get_bool`, `get_float`, `get_string`; the file is TOML, a missing file is an empty config, a broken file
  is a `ConfigError` (`InvalidToml`, `Unsupported`, `Io`), and `save` is a temp file plus rename. `engine::user_config_path(
  org, app)` is `<user data directory>/config.toml`.
- td-over: replaces the game's own `mt19937` holders and its settings reader/writer (E13, E14). Delete them, load the
  config once in `on_start`, and keep the defaults in the `get_*` calls. A game that needs replays seeds the world's
  `Random` from the recorded seed.

## wind-220 fix: strict UI diagnostics, null `AssetId` and GPU context in `AssetsDb::get` (E16, E18)

Problem: `AssetsDb::get(AssetId{})` aborts; unknown binding names and CSS properties fail silently; in the editor
Play, a GPU texture requested from a `Frame` system fails as `Corrupt` (probably no current GL context), and the
transparent game window stays opaque.

Do:
- `get(AssetId{})` returns an error plus a warning instead of aborting.
- A strict UI mode in Debug/editor: unknown binding name or CSS property is an error with the document and line.
- `AssetsDb::get` for GPU types makes the main game window's context current, or returns a distinct
  `AssetError::NoContext`.
- Editor: support the game's transparent window, or an "overlay preview" flag.

Done when: tests (no GPU) cover the null id and strict-UI errors; the `Corrupt` case reproduces and is fixed in the
editor Play; `Assets.md`, `Editor.md` updated.

## wind-221 docs: floating text and tooltip/stacking RFCs (E11, E12)

Problem: td-over implemented floating combat text itself (generic, no game knowledge) and the tooltip/stacking RFC
points at files that no longer exist.

Do: decide whether floating text moves into the engine (use td-over `vfx/floating_text.cpp` as the reference) and
write the design note; refresh the stacking-context/tooltip RFC against the current tree.

Done when: both decisions are recorded in `docs/tech/`, with follow-up `wind-N` tasks if either is accepted.
