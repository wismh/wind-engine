---
tags: [file, ui]
aliases: [include/engine/ui/profiler.h, profiler.h]
---

# `include/engine/ui/profiler.h`

Module: [[modules/UI]]

`ui::set_ui_profiler_enabled` opens or closes the profiler window. Without `ENGINE_UI_PROFILER` (Release and MinSizeRel) those functions are inline no-ops, so a game's debug key still compiles in those configurations. `ProfilerWindowHost` is how the windowed presentation opens and closes the OS window; headless tests leave it empty. `ProfilerPanel` marks the panel canvas so timing and the canvas list skip it. The game calls this; the engine does not bind a key. See [[features/UI Profiler]].

Repo path: `include/engine/ui/profiler.h`
