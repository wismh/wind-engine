#!/usr/bin/env bash
# Smoke test of an installed Wind editor SDK, the way a game and the editor use it (docs/tech/build/CMake.md).
#   tools/ci/sdk_smoke.sh <sdk-dir> <work-dir> [--e2e]
# Makes a project from the SDK's own template, configures and builds its module against the SDK in DebugGame and
# Release (what the editor's Play does), and checks the module record. With --e2e it also starts the editor on the
# project with `wind-cli launch --play --wait`, which needs a display (xvfb on Linux), and stops it.
set -euo pipefail

sdk="$(cd "$1" && pwd)"
work="$2"
e2e="${3:-}"
target="smoke"

version="$(sed -n 's/^version = "\(.*\)"$/\1/p' "$sdk/sdk.toml")"
test -n "$version" || { echo "no version in $sdk/sdk.toml" >&2; exit 1; }

rm -rf "$work"
mkdir -p "$work"
work="$(cd "$work" && pwd)"
cp -R "$sdk/templates/empty" "$work/game"
cd "$work/game"
mkdir -p assets
find . -type f \( -name '*.toml' -o -name '*.txt' -o -name '*.json' -o -name '*.cpp' \) -print0 |
    xargs -0 sed -i.orig \
        -e "s|{{target}}|$target|g" -e "s|{{name}}|Smoke|g" -e "s|{{engine}}|$version|g" -e "s|{{sdk}}|$sdk|g"
find . -name '*.orig' -delete

build="$work/game/build-editor"
# The configure the editor runs (editor/src/project_build.cpp).
cmake -S . -B "$build" -G "Ninja Multi-Config" \
    "-DCMAKE_PREFIX_PATH=$sdk" "-DWind_DIR=$sdk/cmake" "-DCMAKE_CONFIGURATION_TYPES=DebugGame;Release"
for config in DebugGame Release; do
    cmake --build "$build" --config "$config" --target "$target" --parallel
    record="$build/wind/$target.$config.module"
    test -f "$record" || { echo "no module record $record" >&2; exit 1; }
    module="$(cat "$record")"
    test -f "$module" || { echo "module $module was not built" >&2; exit 1; }
    echo "built $module"
done

if [ "$e2e" = "--e2e" ]; then
    cli="$sdk/bin/wind-cli"
    cleanup() { pkill -f "$sdk/bin/wind_editor" || true; }
    trap cleanup EXIT
    if ! "$cli" launch "$work/game" --play --wait 600; then
        echo "--- game.log"
        cat "$sdk/bin/game.log" 2>/dev/null || true
        pkill -f "$sdk/bin/wind_editor" || true
        echo "--- the editor in the foreground for 20 s"
        timeout 20 "$sdk/bin/wind_editor" --project "$work/game" 2>&1 | head -80 || true
        cat "$sdk/bin/game.log" 2>/dev/null || true
        exit 1
    fi
    "$cli" state
    "$cli" stop
    "$cli" state
fi
echo "SDK smoke test passed"
