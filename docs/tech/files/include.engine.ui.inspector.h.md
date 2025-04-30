---
tags: [file, ui]
aliases: [include/engine/ui/inspector.h, inspector.h]
---

# `include/engine/ui/inspector.h`

Module: [[modules/UI]]

`ui::set_inspector_enabled` shows or destroys the per-window inspector panel. `UiInspector` (a `World` context) holds the flag and one `InspectorPick` per window: canvas entity, element path, nearest `generated_owner`, and whether anything is selected. `InspectorPanel` marks the panel canvas so pick, the tree, and the hover box skip it. The game calls this; the engine does not bind a key. See [[features/UI Inspector]].

Repo path: `include/engine/ui/inspector.h`
