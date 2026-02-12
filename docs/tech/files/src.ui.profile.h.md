---
tags: [file, ui]
aliases: [src/ui/profile.h]
---

# `src/ui/profile.h`

Module: [[modules/UI]]

Private scope macro `ENGINE_UI_PROFILE`. Under `ENGINE_UI_PROFILER` it constructs a scope that adds elapsed time into the open frame. Otherwise it is `((void)0)` and its arguments are not evaluated. Not on the public include path. See [[features/UI Profiler]].

Repo path: `src/ui/profile.h`
