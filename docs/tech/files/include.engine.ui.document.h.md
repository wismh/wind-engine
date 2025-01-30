---
tags: [file, ui]
aliases: [include/engine/ui/document.h, document.h]
---

# `include/engine/ui/document.h`

Module: [[modules/UI]]

Element tree, parse_xml, layout, bind, `UiInstance`. Binding attributes hold a `BindingId` ([[include.engine.ui.binding_id.h]]), not a string. `ElementKind::TextInput` is the single-line text input. `ElementKind::Component` and `paint` / `IPaint*` are the custom-draw hole ([[include.engine.ui.paint.h]]). `ElementKind::Viewport` is the pan/zoom camera (`pan-x` / `pan-y` / `zoom` bindings); `hit_test` inverts that camera.

See [[src.ui.document.cpp]].

Repo path: `include/engine/ui/document.h`
