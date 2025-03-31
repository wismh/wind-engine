---
tags: [file, ui]
aliases: [src/ui/element_path.h]
---

# `src/ui/element_path.h`

Module: [[modules/UI]]

Private path between a document root and an `Element`. A plain step indexes `children`. A step into `generated_items` sets `kGeneratedPathBit`. Scrollbar drag and the inspector share it. `resolve_inspector_element` retargets a generated step by `generated_owner` when the row's index has moved.

Repo path: `src/ui/element_path.h`
