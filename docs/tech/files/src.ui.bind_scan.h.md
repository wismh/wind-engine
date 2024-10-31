---
tags: [file, ui]
aliases: [src/ui/bind_scan.h, bind_scan.h]
---

# `src/ui/bind_scan.h`

Module: [[modules/UI]]

Private. `BindBinder` / `BindMember`: walk a parsed UI document and collect `{binding path}` members (plus nested binders, one per `ItemsControl`/`ItemTemplate`) for `asset_codegen` to emit as a `bind(vm)` struct. Implemented in `src/ui/xml_parser.cpp` (`scan_bind_tree`).

See [[src.resources.codegen.cpp]], [[build/Asset Codegen]].

Repo path: `src/ui/bind_scan.h`
