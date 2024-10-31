---
tags: [file, resources]
aliases: [src/resources/codegen.cpp, codegen.cpp]
---

# `src/resources/codegen.cpp`

Module: [[modules/Resources]]

Scan assets tree, emit header + catalog. For `importer = "ui"` XML, also emits a per-document binder struct with one `constexpr BindingId` per `{binding}` path and a `bind(vm)` template — see [[build/Asset Codegen]].

See [[build/Asset Codegen]].

Repo path: `src/resources/codegen.cpp`
