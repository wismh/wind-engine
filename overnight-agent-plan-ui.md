# Overnight task for the cloud agent: engine tasks wind-213 to wind-219 (td-over limits)

You are working alone for many hours and nobody will answer questions. **Never stop to ask. Decide, write the decision
down (see "Decisions log"), and continue.** Stop a task only for a hard blocker you cannot work around (CI service down
for good, no write access). Write it to the blocker list and go on with the next task that does not depend on it. At
the end publish the report in "Final report".

Repository: `wismh/wind-engine` (public), default branch `main`. Work from a fresh clone of `origin/main`. The owner's
local checkout has unrelated uncommitted work (hierarchy); you never see it and must not wait for it.

## Where the tasks come from

`docs/tech/tasks.md` describes wind-213 to wind-221 (problem, what to do, done when). It was written from the game
td-over's `docs/engine-limits.md` (E1-E18). **That file is not on `main` yet**: the owner's checkout has it untracked.
Everything you need from it is repeated below, so do not look for it. Your scope is **wind-213 to wind-219**. Do not do
wind-220 or wind-221 (wind-220 partly overlaps the finished wind-222, see "What changed since the tasks were written").

Read first: `CLAUDE.md` (and `AGENTS.md`), `docs/tech/architecture/Principles.md`, `Boundaries.md`, `Scope.md`,
`docs/tech/modules/UI.md`, `docs/tech/features/UI Markup.md`, `UI Input.md`, `Windowing.md`, `Materials and Sort.md`,
`docs/manual/ui/HUD-and-Controls.md`, `docs/manual/best-practices/Rules-and-Conventions.md`,
`.claude/skills/wind-git/SKILL.md`, `docs/tech/build/CMake.md`, `README.md` (how to build and test on Linux), and the
`.github/workflows/*.yml`. If `~/.claude/skills/wind-cpp/SKILL.md` and `wind-ui-xml-css/SKILL.md` exist, read them before
writing C++ or UI XML.

## What changed since the tasks were written (verified on `origin/main` 6331a80)

Check each point yourself before relying on it; **code wins over docs**.

- **wind-222 (error handling) is merged.** `IFatalError::report` is `noreturn`; `DefaultFatalError` is the default;
  `AssetsDb::get<T>` reports through `IFatalError`, `try_get<T>` returns `AssetError`; host and engine startup return
  `EngineError`. Any new failing path you add follows `docs/manual/best-practices/Rules-and-Conventions.md` (error
  handling section): recoverable gives an error value, a programmer error goes through the fatal hook. Tests use the
  throwing `IFatalError` fake that wind-222 added.
- **wind-197 and wind-196 are merged.** `ui::IBindErrorSink` (`include/engine/ui/document.h`) names a missing binding;
  stylesheet parser warnings reach the log with path and line. New binding kinds you add (wind-213, wind-215) must report
  unknown names through the same sink, not silently.
- **`MaterialOverride` exists** (`include/engine/render/material.h`, used by `render/commands.h`). wind-218 starts from
  that, not from nothing.
- **`IWindowControl`** (`include/engine/core/window_control.h`) already has `open_window`/`close_window`,
  `open_windows`, `raise`, `position`/`size` (live, screen coordinates), `set_drag_region`, `set_click_through_enabled`,
  `work_area`, file dialogs, and window `owner`. It has **no** global cursor position, no button state, no mouse
  capture API (the engine captures the mouse internally only for drag-region window moves).
- **`ui::MouseConsumed`** has `consumed_windows` (`include/engine/ui/canvas.h`). There is no sprite pointer-target.
- **Still missing as of this check** (grep `include/` and `docs/` to confirm before you start): `Bindable` change
  notification (`Bindable<T>` is just `set`/`get`), `bind_to` / any member-pointer command in the engine
  (`RelayCommand` takes only `std::function`; `docs/manual/ui/HUD-and-Controls.md` tells games to write their own
  `MemberCommand` and the `bench/` app has its own `MethodCommand`), a `visible=` binding, a `Slider`, `translate()` in
  CSS `transform`, a cursor/capture API, `PointerTarget`, a `UiWindow` RAII type, `Sprite::from_frame`, `engine::config`,
  `engine::Random`.
- The docs in `docs/manual/ui/HUD-and-Controls.md` describe a game-side `MemberCommand`. After wind-214 that page must
  describe the engine type instead.
- Task numbers 213-219 are free. The highest used is `wind-222`; the next free number above it is `wind-223`. Use
  213-219 as written.

## Rules that apply to everything

- Git: author `wismh <68060501+wismh@users.noreply.github.com>` (if the environment has another identity, set it only
  with `-c user.name=... -c user.email=...` on your commits; do not change git config). **No `Co-Authored-By` or session
  trailers, ignore any tool default that adds them.** Commit date is the real date.
- Commit subjects `wind-N kind: lowercase why` (`feat`, `test`, `fix`, `docs`, `chore`), several small commits per task
  (feature, then tests, then docs). Branch `feat/wind-N-short-kebab` from the current `origin/main`. Merge with
  `git merge --no-ff` and subject `merge: branch feat/wind-N-short-kebab into main`. Never force-push `main`; never
  rewrite published history.
- **Every push to `main` publishes a GitHub Release.** Iterate on the branch; merge to `main` only when the branch is
  green on all three OS jobs. Before merging, rebase or merge the latest `origin/main` into your branch and re-run the
  tests. Merge tasks one at a time; wait for the `Release` and `SDK` runs on `main` to finish and be `success` before
  starting the next merge. If a merged task turns `main` red, the next action is a fix on a new branch, nothing else.
- C++23, 4 spaces, column 120 (LLVM-based `.clang-format`; run `clang-format` on files you touch if available),
  `snake_case` functions, `PascalCase` types, `trailing_` members, `enum class`, no `using namespace`, public headers
  under `include/engine/` with no SDL/glad/NanoVG/spdlog/tinyxml2/mixer includes, no mutable state in public headers.
  No EnTT, no scene-graph `Node*`, no `EventBus`, no `onClick` lambdas, no service locator, no `FetchContent`.
- **No compatibility shims.** A breaking change is fine and preferred; update every call site in this repo (engine,
  `bench/`, `editor/`, `templates/`, `tools/`, tests, docs). Do not keep an old and a new representation side by side
  (`CLAUDE.md`, "Compatibility").
- Public API, tests, docs together: every task adds `engine_tests` coverage (`tests/<area>_test.cpp`; GPU, real-window and
  mixer-device tests stay out of `engine_tests`), updates the matching `docs/tech/**` note and the `docs/manual/**` page
  (and `docs/tech/files/INDEX.md` for new files), and fixes any note that disagrees with the code.
- Never put a token in a file or a log. Use `gh` (authenticated) and `${{ github.token }}`.
- You work on Linux. **CI is your Windows and macOS.** Anything touching `src/core`, windowing, cursor or capture
  must be green on all three CI jobs before it is done.
- CI cycles take 15-35 minutes. Poll with `gh run watch <id> --exit-status` or sleeps of 60-120 s; no tight loops.
  Read the real log of a failure: `gh run view <id> --log-failed`. A run is green only when `gh run view` says
  `success`.

## Local build and test (Linux)

Follow `README.md` and `docs/tech/build/CMake.md`. Use the `tests` preset (or plain
`cmake -S . -B build -DENGINE_BUILD_TESTS=ON`, whichever works) and run `ctest --output-on-failure`. Run the full
`engine_tests` before every push, not only your new tests. If the whole suite is slow, run your area first, the whole
suite before pushing. For anything that needs a window, use `xvfb-run` with `LIBGL_ALWAYS_SOFTWARE=1
SDL_AUDIODRIVER=dummy` for manual checks only; such checks never become `engine_tests`.

## Decisions log

Keep `OVERNIGHT-LOG.md` in your working directory, **not committed** to `main`; it goes into the final issue. One line per
non-obvious choice: what, why. Defaults when something is unclear:

- Pick the simplest design that is correct, tested, and keeps one representation.
- Pick the design with the smaller public API.
- Fail loudly (named error through the existing sink or fatal hook) rather than silently.
- If a task item turns out to be already solved, or impossible without a larger redesign, do not invent work: write
  one paragraph in the log and in the task's doc, and move on. Do not stub or half-wire an option
  (`CLAUDE.md`: "finish the wiring or don't add the value yet").

## Order

Dependencies: wind-215 needs wind-213. wind-217 uses what wind-216 adds. The others are independent. Smallest and
safest first, so a late failure costs least:

1. **wind-214**, 2. **wind-219**, 3. **wind-213**, 4. **wind-218**, 5. **wind-215**, 6. **wind-216**, 7. **wind-217**.

If you run out of night, finish and merge what is green; do not leave a half-done task on `main`. A branch that is not
merged stays pushed with a note in the report.

---

## wind-214 feat: `bind_to` and a member-pointer command in the engine (E2)

Today `RelayCommand` takes only `std::function`; every game writes its own `MemberCommand` or a lambda per button.

Do:
- Add to `include/engine/ui/command.h` an engine member-pointer command (name it `MethodCommand`, the name already used
  by `bench/`, unless the code shows a clash) with `template<typename Owner, void (Owner::*Method)()> void
  bind_to(Owner&)`. No `std::function`, no heap allocation: a pointer to the owner and a plain function pointer. An
  unbound command has `can_execute() == false`. Optional second form with a `bool (Owner::*)() const` for
  `can_execute`; add it only if a caller in this repo needs it.
- Allow binding after construction (the view model exists before its owner). Do **not** add an owner-in-constructor
  variant.
- Keep `RelayCommand` for tests and quick scripting. **Migrate every use in the repo that is a button on an owner**
  (`bench/src/*`, `editor/`, `templates/`, tests) to `MethodCommand`; delete bench's private `MethodCommand`.
- Update `docs/manual/ui/HUD-and-Controls.md` (it currently teaches a game-side `MemberCommand`), the `wind-cpp` rule
  text in `docs/manual` if it lives there, `docs/tech/modules/UI.md`.

Tests: execute calls the right member on the right owner; two owners of the same class; rebind to a new owner; unbound
does nothing and `can_execute` is false; works through the generated `bind()` path as an `ICommand` (use whatever
existing UI test fixture binds a view model).

## wind-219 feat: public `engine::config` and `engine::Random` (E13, E14)

Do:
- `engine::Random`: a small seedable PRNG (PCG32 or xoshiro, pick one, implement in `src/`, no `<random>` engine state in
  a public header; a `std::uniform_*_distribution` is fine inside `.cpp` or avoided: provide `next_u32`, `uniform(lo,
  hi)` for int and float, `chance(p)`, `pick(span)`-style helper only if trivial). Held in `World::ctx<Random>()` or the
  existing context mechanism, seeded at engine start from `EngineConfig`/`GameDesc` with a default of a time-based seed
  and an explicit fixed seed for tests and replays. **Same seed, same sequence on every platform** (do not depend on
  `std::` distributions, whose output is implementation-defined; write the float mapping yourself).
- `engine::config`: read and write a flat or one-level table of strings, ints, floats, bools under
  `engine::user_data_directory` (check the existing helper in `include/engine/` and reuse it). Back it with tomlplusplus
  in `src/` (private); the public header exposes no toml type. Missing file is an empty config; malformed file is an
  error value (not UB, not an exception escaping the API) that keeps the old file untouched; writing is atomic
  (temp file + rename). Typed getters with a default: `get_int("audio.volume", 80)`.
- Docs: new `docs/tech/modules/Core.md` section (or a new note if the vault convention says so) and a manual page.

Tests: Random reproducibility with golden values (pin the first 8 outputs of seed 1 and seed 12345), range and bias
sanity, stream independence if you add streams; config round trip, defaults, type mismatch, malformed input, atomic
write leaves the old file on failure, path under a temp `user_data_directory` override.

## wind-213 feat: bindable change notification, `visible` binding, typed length binding (E1, E3, E4)

Do:
1. **Change notification.** `Bindable<T>::on_changed` is one option; the other is a dirty flag read and cleared by the
   engine. Decide by looking at how `ViewModel` pushes and pulls values now (`include/engine/ui/view_model.h`, "Writes
   through to the Bindable<T> ... if T is arithmetic"). Required behaviour either way: a write **from XML** (drag,
   TextInput, Checkbox) and a write **from C++** are both reported, once per change, and not when the value is
   unchanged. Prefer the design where a game registers one handler per bindable without polling. Keep one
   representation.
2. **Visibility.** `visible="{binding name}"` (a `Bindable<bool>`) on any element, mapped onto the existing
   `ComputedStyle::visible` / `display_none` machinery. Decide `visible` (keeps layout space) vs `display` semantics from
   what `ComputedStyle` already has: the binding must be able to remove the element from layout, since the replaced
   pattern is a 0-or-1-item `ItemsControl`. If both are cheap, offer both as two attributes; otherwise only the removing
   one, named so it cannot be mistaken for `visibility`. `<If>` is out of scope unless it falls out for free.
3. **Typed length.** A `Bindable<float>` (pixels) usable in `var-left`/`var-top`/`var-width`/`var-height` bindings
   without string formatting; keep `Bindable<std::string>` working only if the string path is the same code (do not keep
   a parallel path). Document the unit.
4. Unknown binding names in these new forms report through `IBindErrorSink` with the name (wind-197 style). Generated
   code (`bind()`) must register the new kinds; extend the code generator and its golden test.
5. Update `bench/` if it formats pixel strings or polls, as the proof that the new forms work.

Tests: XML-originated change is reported once; C++ write reported; same value is silent; visibility toggles layout (a
sibling moves); float-bound `left` positions an element at the expected rect; missing name is a named error; generated
binding for each new kind. Docs: `UI Markup.md`, `UI.md`, manual UI pages.

## wind-218 feat: per-instance material parameters and `Sprite` from an animation frame (E10, E15)

Start by reading how `MaterialOverride` (`render/material.h`) is attached to a sprite entity today and who rewrites it
(`src/ecs/`). The td-over pain: a hit flash needs a tag component, a system rewriting the override every frame and a
separate material.

Do:
- **E10.** Make an instance parameter (a named uniform or the existing override fields, whatever `material.h` exposes)
  settable once on the entity and persist until changed, with no per-frame rewrite and **no sharing between two
  entities that use the same material**. If this already works through the existing component, then the work is the
  missing piece: a documented, tested API and a manual example; write that in the log and do not add a new mechanism.
  Anything new goes through `CommandBuffer`/`IMaterial` as the architecture says and stays out of `engine_tests` where it
  needs a GPU (test the data path headlessly: the override values on the produced draw commands).
- **E15.** `Sprite::from_frame(const SpriteFrame&)` (or the engine's actual frame type) copying all the fields a spawn
  needs, and make `SpriteAnimator` initialize the `Sprite` on its first tick so a freshly spawned animated entity needs no
  manual copy. Update the repo's own spawn code and the `templates/` or bench usage to the new call.

Tests: two instances with different values on one material produce different draw data; value persists across frames
without rewriting; `from_frame` copies every field (a test that fails if a field is added to the frame type and not
copied: static assert on field count is acceptable); animator first-tick init. Docs: `Materials and Sort.md`, manual
Sprites and Materials pages.

## wind-215 feat: absolute, drag-bound `ItemsControl` items and a Slider (E5, E9)

Needs wind-213 merged. Read `UI Markup.md` (ItemsControl, ItemTemplate, `drag="{binding}"`, the layout rules for
absolutely positioned children) and the drag code in `src/ui/`.

Do:
- **E5.** Inside an `ItemTemplate`, allow per-item bindings for `var-left`/`var-top` (typed float from wind-213) and for
  `drag="{binding}"`, so a list of view models can be laid out by position and dragged. A parent with
  `position: relative` (or the absolute equivalent the engine uses) contains the items. Z-order per item (a bound
  integer or an order rule) is needed for an inventory; add it only if the current stacking rules cannot express it,
  and then as a typed binding, not a string.
- **E9.** `<Slider value="{binding fraction}" min max step/>` built on the existing `drag="{binding}"` machinery, writing
  a `Bindable<float>`. Horizontal and vertical. Track and handle are styled by CSS classes; **the handle position must not
  require a pixel constant duplicated in C++ and CSS**. Add `translate()` (and `translateX/Y` if the parser is
  per-function) to the CSS `transform` subset only if the Slider needs it or it costs little; otherwise skip it and say
  so.
- Convert one real screen in `bench/` or `editor/` to the new templated absolute items (the bench is a good proof) and
  delete the hand-written duplicates.

Tests: items positioned by binding land at the expected rects; dragging one item updates its own bindable and not its
siblings; item list change (add/remove) keeps the others' state; slider value to rect and rect to value round trip at
min, max, mid, with `step`; missing binding names are named errors. Docs: `UI Markup.md` (new tag, attributes,
`translate()` if added), manual UI pages.

## wind-216 feat: global cursor, mouse capture and pointer targets (E6, E7)

Read `UI Input.md`, `Windowing.md`, `src/core/` window code, `ui/canvas.h` (`MouseConsumed`, click-through logic around
line 219), and `include/engine/core/window_control.h`. Touches platform code: **all three CI jobs must be green**.

Do:
- **E6.** On `IWindowControl` add `cursor_screen_position()` (screen coordinates, same space as `position()`/`size()`;
  `nullopt` when unavailable), `is_mouse_button_down(MouseButton)`, and `capture_mouse(WindowId, bool)` (or deliver mouse
  events to the window that owns the press while a button is held; pick the one that also works with the engine's
  internal drag-region capture without conflict, and write why). Implementation in `src/core/` over SDL
  (`SDL_GetGlobalMouseState`, `SDL_CaptureMouse`); a headless/test double in the existing test windowing fakes. Not
  supported (Wayland, Web) returns `nullopt`/`false` and is documented, never crashes.
- **E7.** A `PointerTarget` component (world-space rect, or derived from `Sprite` + `Transform`) with hover and click
  results exposed as ECS events or a component state (follow how other input results are delivered; do not add an
  `onClick` callback). The input system counts targets under the cursor for click-through **itself**, so a game stops
  writing `consumed_windows` for sprites. Remove the ordering trap: document the order of UI-consumed vs
  pointer-target handling in one place and make the system enforce it, not the caller. Use camera and `WindowId` aware
  hit-testing (a target belongs to the window whose camera shows it).
- Update the repo's own overlay/click-through code (bench, editor, templates) to the new API.

Tests (headless): hit-test inside, outside, rotated/scaled transform, overlapping targets (topmost by sort order wins),
two windows, camera pan/zoom, hover enter/exit events once each, click-through decision with a target under the cursor
and with UI over a target, capture state on a fake window control. Real-window behaviour is checked manually under
`xvfb` and by CI smoke only. Docs: `Windowing.md`, `UI Input.md`, manual input/windowing pages.

## wind-217 feat: RAII `UiWindow` for secondary windows (E8)

Needs the cursor/capture decisions from wind-216 only for the drag-region helper. Read how editor panels and any
multi-window code in `src/` and `editor/` open secondary windows today (grep `open_window`, `bind_window`,
`destroy_canvases_for_window`).

Do: `UiWindow` in `include/engine/` (own header, one type per file): `UiWindow::open(world, WindowDesc, UiCanvasDesc)`
returning the window or an `EngineError`-style value (use the wind-222 conventions); the destructor tears down in the
right order (canvases, `unbind_window`, `close_window`); move-only; a close-requested callback or flag taken from
`WindowCloseRequestedEvent` without a game-side `if-else` over `WindowId`; a drag-region helper that excludes the
bounds of interactive children automatically (the problem the `set_drag_region` WARNING describes); a
centre-on-work-area helper over `work_area()`. Convert the repo's own multi-window code to it and delete the manual
sequences.

Tests (fake window control, no real window): teardown order, move semantics, double close, owner window closing closes
the owned `UiWindow` without double free, close-requested routing for two windows, drag region excludes a button
rect, centring maths. Docs: `Windowing.md`, manual windowing page.

---

## Per-task checklist (run it before every merge)

1. Full `engine_tests` green locally; `clang-format` clean on touched files.
2. `grep` the whole repo for the old shape you removed: no leftover call sites, docs, templates, or bench copies.
3. Docs: tech note, manual page, `docs/tech/files/INDEX.md`, and any note that disagreed with the code, all updated in
   the same branch.
4. Push the branch, wait for the CI jobs on it to be `success` on Linux, macOS, Windows. Fix real causes from the logs.
5. Merge into `main`, push, wait for `Release` and `SDK` on `main` to be `success`.
6. Write the migration note for td-over (below) before moving on.

## Migration notes for the game (td-over)

td-over consumes the released SDK, not this repo, and you cannot touch it. For every task append to
`docs/tech/tasks.md` (committed in the task's own branch) a short section **"Migration"**: old shape, new shape, which
td-over files from the old `engine-limits.md` it replaces (the E-number is enough), and what can be deleted there. Also
set the task's heading status in `docs/tech/tasks.md` to `done (wind-N, release v<version>)` after the real release
exists. Get the release version from `gh release list -R wismh/wind-engine --limit 3`.

## Done when

- Each of wind-214, 219, 213, 218, 215, 216, 217 is merged to `main` and `Release` and `SDK` are `success`, or it is
  listed in the report as not done with the reason and the branch name.
- No red required workflow on `main`.
- `docs/tech/tasks.md` lists status and migration notes for every finished task.

## Final report

Create a GitHub issue in `wismh/wind-engine` titled `Overnight report <date>` with: per task, what is done (links to
commits, merges, runs, releases), the decisions log, anything not verified and why (macOS and Windows seen only through
CI; real window and cursor behaviour only under xvfb), open problems and risks, API changes that break td-over with the
exact migration, and what the owner must do by hand. Also print the same text as the final message. Be honest: do not
call a CI run green unless `gh run view` showed `success`; do not call a feature done if its tests were not run.
