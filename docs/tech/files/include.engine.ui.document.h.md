---
tags: [file, ui]
aliases: [include/engine/ui/document.h, document.h]
---

# `include/engine/ui/document.h`

Module: [[modules/UI]]

Element tree, parse_xml, layout, bind, `UiInstance`. Binding attributes hold a `BindingId` ([[include.engine.ui.binding_id.h]]), not a string. `ElementKind::TextInput` is the single-line text input. A bound `command` sets `Element::disabled` from `!can_execute()` on every bind except `TextInput`, which stays typeable when its Enter-to-submit command cannot run yet. `ElementKind::ScrollView` and `Overflow` (`visible` / `hidden` / `scroll` / `auto`) provide scrollable containers with mouse wheel, hit-testing scroll offset, and CSS scrollbar styling. `ElementKind::Component` and `paint` / `IPaint*` are the custom-draw hole ([[include.engine.ui.paint.h]]). `ElementKind::Viewport` is the pan/zoom camera (`pan-x` / `pan-y` / `zoom` bindings); `hit_test` inverts that camera. `ElementKind::Checkbox` carries a runtime `checked` bool (hit-testable like Button, two-way via `checked="{binding}"` or seeded once by a literal) matched by the CSS `:checked` pseudo-class — no built-in drawn mark, the checked look is plain cascaded style. `hit_test` stays the interactive hit. `hit_test_visual` returns the deepest visible element under the point and `layout_boxes` returns that element's margin, border, and content rects in canvas space (scroll and Viewport already applied).

See [[src.ui.document.cpp]].

Repo path: `include/engine/ui/document.h`
