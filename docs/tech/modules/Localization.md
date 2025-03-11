---
tags: [module]
---

# Localization

String tables addressed by `AssetId`, resolved into UI text at bind time. The game chooses the active locale. The engine does not persist it and does not call `setlocale`.

## Capabilities

- One `.strings` file per locale. The body is TOML. `.meta` is `importer = "strings"`. The locale tag lives in the file (`locale = "uk"`), not in the sidecar.
- Exactly one table in an asset tree has `source = true`. That table is the key authority. `asset_codegen` fails the build when the count is not one, when a table does not parse, or when a UI `{tr}` key is absent from the source table.
- Runtime type is `loc::StringTable`, loaded with `AssetsDb::get<loc::StringTable>`. The game copies tables into `world.ctx<loc::Catalog>()` and calls `set_active` / `set_fallback`. `ctx()` default-constructs an empty catalog, so a game that never translates is unchanged.
- Lookup order in `Catalog::text`: active locale, then fallback, then the key itself. A missing active translation warns once. A key absent from the source table sets `Translated::missing_from_source` and warns once. `apply_bindings` reports that as `UiError::MissingString` when an `IFatalError` is passed (`run_bind` does not pass one, so a frame shows the key).
- Message syntax: `{name}`, `{count, plural, one {…} few {…} many {…} other {…}}`, `#` for the integer inside a branch. `{{` is a literal `{`. `}}` is a literal `}` outside a branch; inside a branch the first `}` closes it. Every plural needs an `other` branch. A plural nested in a branch is a bad pattern and the table fails to load. A missing arg is left as `{name}`.
- Integer categories only. `uk`, `ru`, and `be` use the Slavic cardinal rule (one / few / many). `uk-UA` uses the primary subtag. Every other tag uses English (`1` → one, otherwise other). The sign is ignored for the category.
- `{tr key}` and `{tr key name={binding path}}` on `text` and `content`, in XML and in `ui::Node::text` / `content`. The same attribute cannot be both `{tr}` and `{binding}`. `formula` rejects `{tr}`. `TextInput` is player text and is not translated.
- `run_bind` and the pointer path in `canvas.cpp` pass `&world.ctx<loc::Catalog>()`. The resolved string is written to `Element::text`, so the layout dirty-gate sees a locale change as a text change.
- `set_pseudo(true)` wraps the finished string in `[` `]` and appends `~` so layout tests can catch overflow.

Numbers inside `#` and `{count}` are plain decimal digits, with no grouping. The UI font must contain the glyphs: builtin `font_ui` (Tiny5) is not a Cyrillic face. Point `font-family` at a font asset that has the code points. NanoVG draws left to right and does not shape Arabic, Hebrew, or Indic.

## Public headers

- [[include.engine.loc.catalog.h]]

## Tests

tests/loc_format_test.cpp (plurals and message syntax) · tests/loc_catalog_test.cpp (TOML, fallback, missing key, pseudo, warn-once) · tests/ui_loc_test.cpp (bind and locale switch) · [[tests.assets_test.cpp]] (load + codegen)

## See also

- [[modules/UI]]
- [[modules/Resources]]
- [[build/Asset Codegen]]
- [[features/UI Markup]]
