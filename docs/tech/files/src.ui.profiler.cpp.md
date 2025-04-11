---
tags: [file, ui]
aliases: [src/ui/profiler.cpp]
---

# `src/ui/profiler.cpp`

Module: [[modules/UI]]

The profiler window, its canvas list, and the 120-frame rings. The whole implementation is under `ENGINE_UI_PROFILER` (Debug and RelWithDebInfo), so a Release or MinSizeRel translation unit is empty. CLI capture keeps the clock while the window is closed. See [[features/UI Profiler]].

Repo path: `src/ui/profiler.cpp`
