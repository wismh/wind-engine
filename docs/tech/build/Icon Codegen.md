# Icon codegen

One master PNG becomes the icon files each packager wants. `engine_add_game` runs it when `icon.png` exists next to the `CMakeLists.txt` that called the function, and stores the output directory in the target property `ENGINE_GAME_ICON_DIR`. Windows, macOS, Android, and the web shell read that property. They do not invoke the tool again.

## Tool

```
icon_codegen <input.png> <output_dir>
```

`tools/icon_codegen/main.cpp` links `engine` and calls `icon_codegen_write` (`src/resources/icon_codegen.h`). The header is private, so the executable adds `src/` to its include path.

The PNG must decode, be square, and be at least 1024×1024 (`kIconCodegenMinSize`). Otherwise the build fails. There is no upscale.

| `IconCodegenErrorKind` | When |
| --- | --- |
| `Decode` | not a PNG `decode_png_rgba` can read |
| `NotSquare` | width differs from height |
| `TooSmall` | a side is under 1024 |
| `Io` | an output file could not be written |

Outputs under `output_dir`, each resized with `stbir_resize_uint8_linear` (`src/resources/stb_image_resize2.h`) and encoded with `stbi_write_png_to_mem` (`stb_image_write.h`):

| File | Sizes |
| --- | --- |
| `icon.ico` | 16, 32, 48, 256 |
| `icon.icns` | `icp4` 16, `icp5` 32, `icp6` 48, `ic07` 128, `ic08` 256, `ic09` 512, `ic10` 1024 |
| `mipmap-mdpi/ic_launcher.png` | 48 |
| `mipmap-hdpi/ic_launcher.png` | 72 |
| `mipmap-xhdpi/ic_launcher.png` | 96 |
| `mipmap-xxhdpi/ic_launcher.png` | 144 |
| `mipmap-xxxhdpi/ic_launcher.png` | 192 |
| `favicon.png` | 256 |

## Containers

ICO: `ICONDIR` (reserved 0, type 1, count N), then N `ICONDIRENTRY` records (width and height as `u8`, 0 meaning 256, planes 1, bit count 32), then each size's PNG bytes. Vista and later accept a PNG payload in the entry. There is no BMP path.

ICNS: 8-byte header (`icns` plus a big-endian total length), then chunks of a 4-byte OSType, a big-endian chunk length that includes the 8-byte header, and the PNG.

## How each platform consumes it

| Platform | What CMake does |
| --- | --- |
| Windows | `file(GENERATE)` writes `generated/<target>/icon.rc` with `IDI_ICON1 ICON "<dir>/icon.ico"` (forward slashes) and adds it as a source. The `.ico` does not exist at configure time, so the gate is the target property, not `EXISTS` |
| Apple | `MACOSX_BUNDLE` ON. `icon.icns` is a source with `MACOSX_PACKAGE_LOCATION` `Resources` and `MACOSX_BUNDLE_ICON_FILE` `icon.icns`. This repo has no macOS preset |
| Web | POST_BUILD copies `favicon.png` beside the target. `cmake/web/shell.html` links `href="favicon.png"` |
| Android | The mipmaps are generated. Gradle does not pick them up by itself. See [Game Consumer](Game%20Consumer.md) |

Cross-compiles import `icon_codegen` from `ENGINE_HOST_ICON_CODEGEN`. A missing path is a configure error.

## Tests

`tests/icon_codegen_test.cpp` calls `icon_resize_rgba`, `encode_png_rgba` (`src/resources/png_encode.cpp`, also used by `wind-cli screenshot`), `icon_encode_ico`, `icon_encode_icns`, and `icon_codegen_write` in process and decodes the PNG blobs back with `decode_png_rgba`. It does not spawn the executable.

## See also

- [Asset Codegen](Asset%20Codegen.md)
- [CMake](CMake.md)
