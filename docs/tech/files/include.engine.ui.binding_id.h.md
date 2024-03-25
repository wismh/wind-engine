---
tags: [file, ui]
aliases: [include/engine/ui/binding_id.h, binding_id.h]
---

# `include/engine/ui/binding_id.h`

Module: [[modules/UI]]

`BindingId` (interned XML binding path) + `constexpr intern(path)` (FNV-1a hash). `is_bound(BindingId)`; `std::hash<BindingId>` specialization for use as a map key.

See [[include.engine.ui.document.h]], [[include.engine.ui.view_model.h]].

Repo path: `include/engine/ui/binding_id.h`
