---
tags: [file, ui]
aliases: [tests/ui_layout_hit_test.cpp, ui_layout_hit_test.cpp]
---

# `tests/ui_layout_hit_test.cpp`

Module: [[modules/UI]]

Hit-test layout uses the same `IUiPainter::measure_text` as paint when `UiLayoutPainters` is set; without it, hug text uses the CPU fallback and later siblings miss the painted button.

See [[features/UI Input]].

Repo path: `tests/ui_layout_hit_test.cpp`
