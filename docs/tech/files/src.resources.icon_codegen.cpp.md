---
tags: [file, resources]
aliases: [src/resources/icon_codegen.cpp, icon_codegen.cpp, src/resources/icon_codegen.h, icon_codegen.h]
---

# `src/resources/icon_codegen.cpp`

Module: [[modules/Resources]]

Validate one master PNG (square, >= 1024x1024), resize it down with vendored `stb_image_resize2.h`, and emit `icon.ico`, `icon.icns`, the Android `mipmap-*/ic_launcher.png` set, and `favicon.png` via vendored `stb_image_write.h`. See [[build/Icon Codegen]] for the container formats and OSType table.

Declared in the sibling private header `src/resources/icon_codegen.h` (no window/SDL dependency — this directory builds unconditionally into `engine`, including the default headless preset).

See [[build/Icon Codegen]].

Repo path: `src/resources/icon_codegen.cpp`
