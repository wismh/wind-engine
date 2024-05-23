---
tags: [build]
---

# Icon codegen

Host tool for turning one master PNG into the icon files each platform packaging step wants. `engine_add_game` runs it once per game target (§ CMake below) and records the output directory on the `ENGINE_GAME_ICON_DIR` target property; each platform's packaging block reads that property instead of invoking the tool itself.

## `icon_codegen`

[[tools.icon_codegen.main.cpp]] links `engine` and calls [[src.resources.icon_codegen.cpp]]:

```
icon_codegen <input.png> <output_dir>
```

Validates the input decodes as PNG ([[src.resources.png_decode.cpp|decode_png_rgba]]), is square, and is at least 1024x1024 — anything else is a build-time error (`IconCodegenErrorKind::Decode` / `NotSquare` / `TooSmall`), not a silent fallback. From the validated master it resizes down (never up) and writes into `output_dir`:

- `icon.ico` — Windows ICO container, sizes 16/32/48/256.
- `icon.icns` — macOS ICNS container, `icp4`/`icp5`/`icp6`/`ic07`/`ic08`/`ic09`/`ic10` (16 up to 1024).
- `mipmap-{m,h,xh,xxh,xxxh}dpi/ic_launcher.png` — Android launcher baseline (48/72/96/144/192).
- `favicon.png` — 256x256.

Resize is `stbir_resize_uint8_linear` from vendored `stb_image_resize2.h`; each output size is encoded independently via `stbi_write_png_to_mem` from vendored `stb_image_write.h` (same family, same author, as the already-vendored `stb_image.h`).

## `.ico` container

`ICONDIR` (reserved=0, type=1, count=N) + N `ICONDIRENTRY` records (width/height as `u8`, 0 meaning 256; planes=1; bitCount=32; `bytesInRes`/`imageOffset` into the trailing blob region), followed by each size's PNG bytes back to back. Modern Windows accepts PNG-encoded ICONDIRENTRY payloads directly (Vista+), so there's no BMP/DIB path to hand-roll.

## `.icns` container

8-byte header (`'icns'` + big-endian `u32` total length) then chunks: 4-byte OSType tag + big-endian `u32` chunk length (length includes the 8-byte chunk header) + raw PNG payload. The plain-PNG OSType table (`icp4`=16, `icp5`=32, `icp6`=48, `ic07`=128, `ic08`=256, `ic09`=512, `ic10`=1024) was checked against the Apple Icon Image format reference before hardcoding — a wrong 4-byte tag would silently produce a file Finder/iconutil can't read.

## Testability

The library functions (`icon_resize_rgba`, `icon_encode_png`, `icon_encode_ico`, `icon_encode_icns`, `icon_codegen_write`) are plain functions in `engine` — [[icon_codegen_test.cpp]] calls them directly (decoding the embedded PNG blobs back out with `decode_png_rgba` to check pixel dimensions) rather than shelling out to the built executable.

## CMake

Mirrors `asset_codegen`/`asset_guid`: `ENGINE_HOST_ICON_CODEGEN` cache var supplies a native binary when `CMAKE_CROSSCOMPILING` (imported as `IMPORTED GLOBAL` + `IMPORTED_LOCATION`); otherwise `add_executable(icon_codegen ...)` links `engine` directly. Unlike the asset tools, `icon_codegen`'s library logic lives under `src/resources` (private) rather than `include/engine`, so the target additionally gets `engine`'s private `src/` include dir so `main.cpp` can reach `resources/icon_codegen.h`.

On `APPLE`, `engine_add_game` also sets `MACOSX_BUNDLE ON` on the game target and, when `ENGINE_GAME_ICON_DIR` is set, adds the shared `icon.icns` output as a source with `MACOSX_PACKAGE_LOCATION "Resources"` plus `MACOSX_BUNDLE_ICON_FILE "icon.icns"` — the two CMake target properties the default `Info.plist` template (`MacOSXBundleInfo.plist.in`) substitutes into `CFBundleIconFile` and copies the file into `<app>.app/Contents/Resources/`. No macOS preset exists in this repo to build/run the bundle (SDD §17).

## See also

- [[build/Asset Codegen]]
- [[modules/Resources]]
- [[src.resources.icon_codegen.cpp]]
