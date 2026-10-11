# Overnight task for the cloud agent: Wind release CI, wind-launcher repo, SDK downloads

You are working alone for many hours and nobody will answer questions. **Never stop to ask. Decide, write the decision
down (see "Decisions log"), and continue.** The only reason to stop a part is a hard blocker you cannot work around (no
write access, a service is down for good). Then write it to the blocker list and go on with whatever part is still
possible. At the end publish the report described in "Final report".

Repositories (owner `wismh`, both public):
- `wismh/wind-engine` - the engine, the editor, the SDK, the CI. Default branch `main`.
- `wismh/wind-launcher` - exists and is probably empty. It will hold the launcher.

Read first in `wind-engine`: `docs/tech/build/Release.md`, `docs/tech/build/CMake.md`, `docs/tech/features/Launcher.md`,
`docs/tech/modules/Project.md`, `.github/workflows/release.yml`, `.github/workflows/sdk.yml`,
`.github/actions/setup-wind/action.yml`, `tools/ci/{next_version,setup_sdk}.py`, `launcher/`, `tools/ci/sdk_smoke.sh`,
`.claude/skills/wind-git/SKILL.md`, and `docs/manual/getting-started/Continuous-Integration.md`.
If `~/.claude/skills/wind-cpp/SKILL.md` exists, read it before writing C++.

## Rules that apply to everything

- Git, both repos: author `wismh <68060501+wismh@users.noreply.github.com>` (do not change git config for other
  authors; if the environment has a different identity, set it only with `-c user.name=... -c user.email=...` on your
  commits). **No `Co-Authored-By` or session trailers.** Commit date is the real date (no backdating).
- `wind-engine` commit subjects: `wind-N kind: lowercase why` (`feat`, `test`, `chore`, `fix`); `N` is one more than the
  highest `wind-N` in `git log` (it is `wind-203` now, so start at `wind-204`); branch `feat/wind-N-short-kebab` from
  `main`; merge into `main` with `git merge --no-ff` and the subject `merge: branch feat/wind-N-... into main`. Never
  force-push `main`. Never rewrite published history.
- `wind-launcher` has its own history and no task counter: plain `kind: why` subjects (`feat: ...`, `fix: ...`,
  `test: ...`, `chore: ...`), branches `feat/short-kebab`, merge into `main` with `--no-ff` and
  `merge: branch feat/short-kebab into main`. Same no-trailer, no-force-push rules.
- C++: C++23, 4 spaces, column 120, `snake_case` functions, `PascalCase` types, `trailing_` members, `enum class`, no
  `using namespace`, only `<engine/...>` includes, no `if (optional_)` checks on deferred-init optionals, UI buttons are
  `ICommand` methods (no lambda per button), no singletons or service locators. Breaking changes are preferred over
  compatibility shims: update every caller.
- Every push to `wind-engine` `main` publishes a GitHub Release (Part 1). Do not push work-in-progress to `main`.
  Iterate on branches, merge to `main` only when the branch is green.
- Do not delete releases or tags you did not create yourself. The stray tag `v.0.1` is known garbage and is ignored by
  `tools/ci/next_version.py`; leave it. Test releases you create (for example `v0.1.N` from a dry run) are allowed, but
  prefer dry runs that publish nothing.
- Never put a token in a file or a log. Use `gh` (it is authenticated) and `${{ github.token }}` in workflows.
- You cannot run Windows or macOS locally. **CI is your Windows and macOS.** Every change to cross-platform code must be
  green on all three CI jobs before you call it done.
- CI cycles take 15-35 minutes. Poll with `gh run watch <id> --exit-status` or with sleeps of 60-120 s between checks;
  do not poll in a tight loop. Always read the real log of a failure: `gh run view <id> --log-failed`.

## Decisions log

Keep `OVERNIGHT-LOG.md` at the root of whichever repo you work in (do not commit it to `main` of `wind-engine`; put
it in the final GitHub issue instead, see "Final report"). For every non-obvious choice write one line: what, why. Make
the choice yourself using the defaults below.

Defaults when something is unclear:
- Prefer the simplest thing that is correct and tested.
- Prefer using tools that exist on every runner (`curl`, `tar`, `cmake`, `python3`) over adding a dependency.
- Prefer failing loudly with a message that says what to do over a silent fallback.
- If two reasonable designs exist, pick the one that keeps the engine API unchanged.

---

## Part 1 - make the engine CI green, and prove the release pipeline works

### Known failure (verified, run 38094618953)

`.github/workflows/release.yml`, job `SDK 0.1.0a (Windows)`, step Configure: the folded `run: >` block passes
`-DENGINE_VERSION_PATCH=$env:WIND_PATCH` to `cmake` under `pwsh`. PowerShell does not expand `$env:...` inside a token that
starts with `-D...=`, so CMake received the literal text `$env:WIND_PATCH` and the version check in `CMakeLists.txt`
(correctly) stopped the configure. macOS and Linux were cancelled by `fail-fast`.

Fix: do not let the shell expand it. Use the GitHub expression `${{ needs.version.outputs.patch }}` in the command, or
quote the argument as `"-DENGINE_VERSION_PATCH=$env:WIND_PATCH"`. Check every other place in the workflows that
passes a PowerShell variable inside a `-D` argument.

Also look at the `SDK` workflow run of the same commit (38094618967). If it fails, diagnose that too.

### Steps

1. Create branch `feat/wind-204-release-ci-fix` from `main`.
2. Add a `workflow_dispatch` trigger to `release.yml` with a boolean input `publish` (default `false`). When it is
   `false` the workflow does everything (version, build, test, install, pack, upload artifacts) and the `publish` job
   is skipped, and the `version` job must not fail because of a branch ref. Run the version computation from the
   checked-out branch history as it does now. This gives you a dry run on any branch.
3. Fix the known failure. Push the branch. Run
   `gh workflow run release.yml --ref feat/wind-204-release-ci-fix -f publish=false`, find the run
   (`gh run list --workflow release.yml --branch ... --limit 1`), and wait for it.
4. If any job fails, read the log, fix the real cause, push, run again. Repeat until Linux, macOS, and Windows are all
   green. Expected trouble spots (not an exhaustive list; trust the logs): the `dirty = false` check in `sdk.toml`
   (build trees must be ignored by git on the runner), the Windows `cmake --install` that now also builds the player
   libraries (about 13 minutes; the job timeout is 150 minutes), the Linux player libraries and `xvfb`, `sdk_smoke.sh`
   on macOS, archive names, and the `Pack` step (`cmake -E tar --format=zip`, the single top directory
   `wind-sdk-<ver>-<os>-<arch>/`). Also confirm the `ENGINE_VERSION_PATCH` value reaches `sdk.toml` as `version =
   "0.1.<patch>a"` on all three.
5. Make sure the regular `SDK` workflow still passes on the branch.
6. Merge to `main` (`--no-ff`, `merge: branch feat/wind-204-... into main`) and push. This starts the real `Release`
   and `SDK` workflows. Wait for both. If the real `Release` fails for a reason the dry run could not show, fix on a
   new branch the same way and merge again (each merge is a new version; that is fine).
7. After a successful real release verify, with `gh release view v<version> -R wismh/wind-engine --json assets,isPrerelease`:
   - the release is marked prerelease (the stage is `a`),
   - it has exactly 6 assets: `wind-sdk-<ver>-windows-x86_64.zip`, `wind-sdk-<ver>-linux-x86_64.tar.gz`,
     `wind-sdk-<ver>-macos-arm64.tar.gz`, and a `.sha256` for each,
   - each `.sha256` matches its archive (download them and check).
8. Prove `setup-wind` works against the real release. Add `.github/workflows/setup-wind-check.yml`
   (`workflow_dispatch` and `workflow_run` of `Release` when it succeeds) with a matrix of the three OSes. Each job
   uses `./.github/actions/setup-wind` with an explicit `version` input (the version just released), then checks that
   `$WIND_SDK_DIR/sdk.toml` has that version, then creates a project from `templates/empty` (substitute
   `{{name}}`, `{{engine}}`, `{{target}}`) and runs
   `wind_editor --batch --project <dir> --export <out> --log-file <log>` and checks exit code 0 and that the
   exported executable exists. On Linux use `xvfb-run` only if the editor needs it for `--batch` (it should not), and set
   `SDL_AUDIODRIVER=dummy`. On Windows wait for the GUI-subsystem editor with
   `Start-Process -Wait -PassThru` and read the log file. Fix whatever breaks (in `setup_sdk.py`, the action, the
   workflow, or the engine) until the three jobs are green.
   Export on Linux and macOS has never been run; if it fails for an engine reason, fix the engine cause (small,
   focused commits, tests) rather than weakening the check. If macOS export genuinely cannot work within this
   night, make the macOS job check only the download, extraction, and `sdk.toml`, and record exactly why in the
   report.
9. Update `docs/tech/build/Release.md` and `docs/manual/getting-started/Continuous-Integration.md` for anything that
   turned out different from what they say.

### Done when (Part 1)

- Latest `Release` and `SDK` runs on `main` are `success`; verified with `gh run list`.
- A GitHub release `v0.1.N` exists with the 6 correct assets, checksums verified.
- `setup-wind-check` is green on Linux, macOS, and Windows against that real release.
- `main` has no red required workflow.

---

## Part 2 - move the launcher to `wismh/wind-launcher`

Start only after Part 1 is done, because the launcher repo builds against the published SDK.

### Target shape

`wind-launcher` is an ordinary Wind game project (the launcher already uses only public `<engine/...>` headers):

```
wind-launcher/
  wind_project.toml         name = "Wind Launcher", engine = "<released engine version>", target = "wind_launcher"
  CMakeLists.txt            cmake_minimum_required, project(), find_package(Wind REQUIRED), engine_add_game(wind_launcher ...)
  assets/                   the launcher's ui/css/loc assets
  src/  tests/              the launcher code and tests
  .github/workflows/        ci.yml, release.yml
  README.md  .gitignore
```

### Steps

1. Clone `wind-launcher`. Look at what is in it. If it already has content that is not obviously an empty
   initial commit (a README or license only), keep it and add to it.
2. Bring the code in **with history**: in a temporary clone of `wind-engine` run
   `git subtree split -P launcher -b launcher-split` (or `git filter-repo --subdirectory-filter launcher` if
   available), then pull that branch into `wind-launcher`. Keep the history of `launcher/src`, `launcher/assets`,
   `launcher/tests`. Do not copy engine files.
3. Port the build to a game-style `CMakeLists.txt`: `find_package(Wind REQUIRED)` and `engine_add_game(wind_launcher
   <sources>)`; keep the `_wind_launcher_parts` split so `wind_launcher_tests` compiles the same sources without the UI;
   `MACOSX_BUNDLE OFF` for `wind_launcher` (the launcher installs as a plain executable with `assets/` beside it, as
   before); tests link `GTest::gtest_main` (the SDK provides it); the test that builds a project from the engine's
   template must take the template from `<sdk>/templates` (`WIND_SDK_DIR`) instead of `WIND_ENGINE_SOURCE_DIR`.
   The icon, `asset_ids.h` and `catalog.toml` come from `engine_add_game`, as for any game. Remove everything that
   refers to `ENGINE_LAUNCHER` or the engine source tree.
4. Build it the way an export is built (`docs/manual/platforms/Desktop.md`): configure with `-DWIND_EXPORT=ON
   -DCMAKE_PREFIX_PATH=<sdk> -DWind_DIR=<sdk>/cmake -DCMAKE_CONFIGURATION_TYPES=Release` (and
   `-G "Ninja Multi-Config"` on Linux and macOS), build Release. You get the SDK for local work on Linux from the
   release with `tools/ci/setup_sdk.py` (from `wind-engine`) or by downloading the archive with `gh release download`.
5. CI `.github/workflows/ci.yml` (push and pull request): matrix Linux, macOS, Windows; install the SDK with
   `uses: wismh/wind-engine/.github/actions/setup-wind@main` (the version comes from `wind_project.toml`); configure,
   build, run `wind_launcher_tests` (under `xvfb-run` on Linux, `SDL_AUDIODRIVER=dummy`), package.
6. Release workflow `.github/workflows/release.yml` for the launcher, **its own version**, independent of the engine:
   - Version `<major>.<minor>.<patch><stage>` with the same rules as the engine (copy `tools/ci/next_version.py`
     into the repo and adapt it to read major/minor/stage from a `version.txt` file at the repo root; the patch is the
     highest existing tag `v<major>.<minor>.*` plus one). Start at `0.1.0a`. Every push to `main` releases.
   - Build on the three OSes, test, package as `wind-launcher-<ver>-<os>-<arch>.zip` (Windows) or `.tar.gz`
     (Linux, macOS) with `.sha256`. The archive has one top directory `wind-launcher-<ver>-<os>-<arch>/` with the
     executable and `assets/`.
   - Publish a GitHub Release `v<ver>` (prerelease for `a`/`b`).
   - Also keep a rolling release with the tag `latest` (title "Latest launcher", not a prerelease, `--latest`):
     delete and recreate it (or edit its assets) on every release, and attach the same archives with **version-less
     names**: `wind-launcher-windows-x86_64.zip`, `wind-launcher-linux-x86_64.tar.gz`,
     `wind-launcher-macos-arm64.tar.gz`. GitHub's "latest release" skips prereleases, so without this a stable
     download URL (`https://github.com/wismh/wind-launcher/releases/latest/download/wind-launcher-windows-x86_64.zip`)
     would not exist while the launcher is in alpha.
   - A dry-run `workflow_dispatch` input `publish=false`, as in Part 1; use it to get it green before the first real
     publish.
7. `README.md`: what the launcher is, download links (the stable `latest` URLs above), supported platforms, that the
   macOS build is not signed (Gatekeeper warning; right-click -> Open) and the Windows build is not signed
   (SmartScreen), how to build it from source with an SDK, where SDKs and projects are stored.
8. When the first launcher release is published and verified (assets, checksums, the stable URL answers with a
   redirect: `curl -sIL <url>`), **remove the launcher from `wind-engine`** in one branch
   `feat/wind-N-launcher-moved` (use the next `wind-N`): delete `launcher/`, the `ENGINE_LAUNCHER` option and
   everything that depends on it in `CMakeLists.txt`, the `vs-launcher` preset and other launcher presets in
   `CMakePresets.json`, the launcher jobs in `.github/workflows/sdk.yml`, `docs/tech/features/Launcher.md` (leave a
   short page that says the launcher lives in `wismh/wind-launcher` and links its releases), `docs/tech/files/INDEX.md`
   entries, mentions in `docs/tech/**` and `docs/manual/**` (Quickstart and others: point to the launcher releases),
   `tests/cmake_sanity_test.cpp` expectations, and any `out/launcher` leftovers in docs. Grep for `launcher` and
   `ENGINE_LAUNCHER` and check each hit. Keep `templates/` (the SDK installs it and the launcher reads it). Run the
   engine tests and the `SDK` workflow; merge when green.

### Done when (Part 2)

- `wind-launcher` `ci` is green on three OSes and a launcher release exists with correct assets, and the stable
  `latest` URLs download the real archives.
- `wind-engine` has no launcher code or targets and its `SDK` and `Release` workflows are green after the removal.

---

## Part 3 - the launcher downloads any SDK version published on GitHub

Work in `wind-launcher` (after Part 2 step 6 is green; you may develop on a branch while the release is still being
finished). Read first: `src/sdk_catalog.*`, `src/launcher_app.cpp`, `src/launcher_state.*`, `src/user_paths.*`,
`src/sdk_row_view_model.*`, `assets/ui/*.xml`, and in the engine `include/engine/process/process_launcher.h`,
`include/engine/net/` (the HTTP client), `include/engine/project/engine_version.h`, and
`tools/ci/setup_sdk.py` in `wind-engine` (the Python reference for download, checksum, and extraction rules).

### Behaviour to build

On the SDKs page the launcher shows two lists: **Installed** (as today) and **Available**.

1. **Available list.** Fetch `GET https://api.github.com/repos/wismh/wind-engine/releases?per_page=100` (follow `Link`
   pagination until done or until 500 releases). Use only releases that are not drafts and have the SDK asset for **this
   platform and architecture**: `wind-sdk-<ver>-<os>-<arch>.<zip|tar.gz>` plus its `.sha256` (names as in
   `tools/ci/setup_sdk.py`). Parse `<ver>` with `engine::parse_engine_version`; sort newest first with its ordering;
   ignore assets whose version does not parse. For each row show the version, the stage as text (alpha, beta, release),
   the release date, the archive size, and whether it is already installed. Mark the newest release version.
   Cache the response on disk in the launcher's user data directory (with the `ETag`, send `If-None-Match`; on 304 or on
   a network error show the cached list with a one-line note "offline: showing the list from <date>"). Handle the
   unauthenticated rate limit (60 requests/hour): on 403/429 show the cache and a note; if the environment variable
   `GITHUB_TOKEN` or `GH_TOKEN` is set, send it as a bearer token.
2. **Install.** Pressing Install on a row downloads the archive and its `.sha256`, verifies the SHA-256, extracts into a
   temporary directory inside the SDK install directory (`sdk_install_directory()`), checks that the extracted
   top-level directory is `wind-sdk-<ver>-<os>-<arch>/`, that it contains `sdk.toml` whose `version` equals `<ver>`, and
   that `bin/wind_editor` (with `.exe` on Windows) exists, then moves it atomically (rename) to
   `<install dir>/sdk-<ver>`. Any failure removes the temporary directory and the partial download and shows the reason on
   the row. The archive is never executed. Reject archive entries that would leave the target directory (zip-slip,
   absolute paths, `..`); if you extract with the system `tar`, list the archive first and refuse such entries.
   Show progress (bytes downloaded of the asset size from the API, polled from the file size or from the transfer),
   allow Cancel, allow one install at a time, and keep the UI responsive (no blocking calls on the frame loop; use the
   engine's process/HTTP call objects that deliver results between frames).
3. **Uninstall** keeps working as it does now (`delete_sdk`). Also clean leftovers of an interrupted install on start
   (the same way `remove_deleted_sdks` cleans interrupted deletes).
4. **A project needs a version that is not installed.** Where the launcher now says the project's SDK is missing, show
   "Install <ver>" if that version is in the Available list.
5. After an install the new SDK appears in the Installed list at once (re-run `find_sdks`).

### How to do the network and archive work

First check what the engine offers on each platform: `include/engine/net/` (`IHttpClient`). Reports from earlier work say
the HTTP backend exists on Windows, Android, and Web only; on Linux and macOS requests answer `Unsupported`. Verify
this in the code. Decide:
- If HTTP works on all three desktop platforms, use it for the API call and the download.
- Otherwise use `curl` through `engine::IProcessLauncher::run` on all desktop platforms (it ships with Windows 10 1803+,
  macOS, and every common Linux; use `curl -fsSL --retry 3 -H "Accept: application/vnd.github+json"` and `-o <file>`
  for the download), and `tar -xf` for extraction (`tar` handles `.zip` on Windows 10+ and macOS, `.tar.gz` everywhere).
  If `curl` or `tar` is missing, show a clear message on the page. One code path for all three platforms is better
  than two.
Implement SHA-256 in C++ inside the launcher (a small, tested implementation; test it with the standard vectors:
empty string, `abc`, the 448-bit message, and one million `a`) so no hashing tool is needed.

Put the logic behind small interfaces so tests need no network and no UI: a release feed (`fetch_releases`), a downloader,
an extractor, and an installer that composes them, plus a pure function that selects the asset for a platform. Follow the
launcher's existing pattern (state and logic in `_wind_launcher_parts`, view-models thin). UI buttons are `ICommand`
methods on view-models (`MethodCommand`), no lambdas.

### Tests (all must run without network)

- Release JSON parsing and asset selection from recorded fixtures in `tests/fixtures/`: several releases, a prerelease,
  a release lacking this platform's asset, a draft, a malformed asset name, empty and invalid JSON, the pagination
  `Link` header.
- Version ordering of the Available list, "installed" marking, "newest" marking.
- ETag cache: first fetch stores, 304 uses the cache, network error uses the cache with the offline note, rate-limit
  response uses the cache.
- SHA-256 vectors; checksum file parsing (`<hex>  <name>` and `<hex> *<name>`); checksum mismatch rejects.
- Installer with a fake downloader/extractor: success moves to `sdk-<ver>`; wrong top directory, missing `sdk.toml`,
  wrong `version`, missing editor binary, checksum mismatch, extraction failure, zip-slip entries, and cancel all clean
  up and leave the Installed list unchanged; a second install while one runs is refused; interrupted-install leftovers
  are removed on start.
- Real archives: build a small `.zip` and `.tar.gz` in the test with the real tool of the platform, or ship tiny
  fixtures, and extract them with the real extraction path.

### Verify for real (Linux, in your environment, plus CI)

1. Build the launcher on Linux against the released SDK and run the tests.
2. Run the launcher under `xvfb-run` with `LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy` and a temporary data directory
   (override `HOME`/`XDG_DATA_HOME` so you do not touch anything real). Use `wind-cli` against the running launcher
   (`tree`, `click`, `screenshot`; see `docs/tech/features/CLI.md` in `wind-engine`) to open the SDKs page, wait for
   the Available list, click Install on a real release of `wismh/wind-engine`, and wait until it is installed. Check on
   disk that `<install dir>/sdk-<ver>/sdk.toml` has the right version and the editor binary exists and runs
   (`<sdk>/bin/wind_editor --batch` without arguments must print its usage error and exit 2). Take screenshots of the
   page before, during, and after the install and keep them for the report. Then uninstall it from the UI and check the
   directory is gone.
3. Add a CI job in `wind-launcher` that runs this end-to-end install on each OS if you can make it reliable (GitHub
   token from `${{ github.token }}` to avoid the rate limit); at minimum on Linux. Windows and macOS: CI runs the
   unit and integration tests.
4. Update `README.md` and the docs in `wind-engine` that mention how users get SDKs (`Release.md`,
   `Continuous-Integration.md`, manual `Quickstart.md`): the launcher is the normal way, `setup-wind` is for CI.

### Done when (Part 3)

- A release of `wind-launcher` contains the feature and `ci` is green on three OSes.
- You installed a real SDK through the launcher UI on Linux and saw it listed, then removed it, with evidence
  (screenshots and the on-disk check) in the report.

---

## Order and time

Part 1 -> Part 2 -> Part 3. Commit and push small steps; do not hold hours of work uncommitted. If a part is blocked
for hours by something external, record the blocker and continue to the next part that does not depend on it (Part 3
code and tests can be written before Part 2 is finished, against a local clone).

## Final report

When finished (or when nothing more can be done), create a GitHub issue in `wismh/wind-engine` titled
`Overnight report <date>` with: what is done in each part (with links to runs, releases, commits), the decisions log,
what could not be verified and why (for example macOS or Windows behaviour you could only see through CI), open
problems and risks, and exactly what the owner must do by hand (for example enable a repository setting). Also print the
same text as your final message. Be honest: if a check was not run, say so; do not report a CI run as green unless you
saw `success` for it in `gh run view`.
