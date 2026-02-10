---
tags: [file, ui]
aliases: [include/engine/ui/inspector.h, inspector.h]
---

# `include/engine/ui/inspector.h`

Module: [[modules/UI]]

`ui::set_inspector_enabled` opens or closes the inspector window. `UiInspector` (a `World` context) holds the flag, `pick_pointer` (mouse selection, on by default), that window, the game window whose pick the detail block shows, and one `InspectorPick` per game window: canvas entity, element path, nearest `generated_owner`, and whether anything is selected. `InspectorWindowHost` is how the windowed presentation opens and closes the OS window; headless tests leave it empty. `InspectorPanel` marks the panel canvas so pick, the tree, and the hover box skip it. The game calls this; the engine does not bind a key. See [[features/UI Inspector]].

Repo path: `include/engine/ui/inspector.h`
