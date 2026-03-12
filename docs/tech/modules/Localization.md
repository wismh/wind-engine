# Localization

String tables addressed by `AssetId`, resolved into UI text at bind time. The game chooses the active locale. The engine does not persist it and does not call `setlocale`.

## Tables

One `.strings` file per locale. The body is TOML. The sidecar is `importer = "strings"`. The locale tag is in the file (`locale = "uk"`), not in the sidecar.

`parse_string_table` (`include/engine/loc/catalog.h`) requires `locale` and, for each `[[string]]`, `id` and `text`. `note` is ignored. A pattern that is not valid message syntax fails the whole table.

| `StringTableError` | Meaning |
| --- | --- |
| `InvalidToml` | TOML did not parse |
| `MissingLocale` | no `locale` |
| `EmptyId` | blank `id` |
| `DuplicateId` | the same `id` twice |
| `MissingText` | a row without `text` |
| `BadPattern` | message syntax failed |

Exactly one table in an asset tree has `.meta` `source = true` whenever the tree contains any `.strings` file. That table is the key authority. `asset_codegen` fails the build when the count is not one, when a table does not parse, or when a UI `{tr}` key is absent from the source table. `{tr}` with no source table fails as well.

Runtime load is `AssetsDb::get<loc::StringTable>`. The game copies tables into `world.ctx<loc::Catalog>()` with `add(table, Role)`. `ctx()` default-constructs an empty catalog, so a game that never translates is unchanged.

## Lookup

`Catalog::text` order: active locale, then fallback, then the key itself.

| Situation | Result |
| --- | --- |
| Missing active translation, key present in the source table | fallback or the key. Warns once. `missing_from_source` stays false |
| Key absent from the source table | `missing_from_source` true. Warns once |
| `apply_bindings` sees `missing_from_source` and an `IFatalError` was passed | `UiError::MissingString` |
| `run_bind` | does not pass `IFatalError`, so the frame shows the key |

`set_active` and `set_fallback` take locale tags. `set_pseudo(true)` makes `text` pass the finished string through `pseudolocalize`: `[` + text + `max(1, text.size() / 3)` tildes + `]`.

`run_bind` and the pointer path in `canvas.cpp` pass `&world.ctx<loc::Catalog>()`. The resolved string is written to `Element::text`, so the layout dirty-gate sees a locale change as a text change.

## Message syntax

Implemented in `src/loc/format.cpp` and `src/loc/plural.cpp`.

- `{name}` substitutes an `Arg`. A missing arg is left as `{name}`.
- `{{` is a literal `{`. `}}` is a literal `}` outside a branch. Inside a branch the first `}` closes it.
- `{count, plural, one {…} few {…} many {…} other {…}}`. Every plural needs `other`. `#` inside a branch is the integer. A plural nested in a branch is a bad pattern and the table fails to load.
- Categories are integers only. The sign is ignored. `uk`, `ru`, and `be` use the Slavic cardinal rule (one / few / many). A tag such as `uk-UA` uses the primary subtag. Every other tag uses English (`1` is one, otherwise other).
- Numbers inside `#` and `{count}` are plain decimal digits, with no grouping.

## Where `{tr}` is legal

`text` and `content`, in XML and in `ui::Node::text` / `content`. `formula` rejects `{tr}`. `TextInput` is player text and is not translated.

Builtin `font_ui` is Inter, which covers Latin, Greek, and Cyrillic. A face that lacks a code point needs its own `font-family`. NanoVG draws left to right and does not shape Arabic, Hebrew, or Indic.

## Public header

`include/engine/loc/catalog.h`

Plural and format helpers stay in `src/loc/`.

## Tests

`tests/loc_format_test.cpp`, `tests/loc_catalog_test.cpp`, `tests/ui_loc_test.cpp`, `tests/assets_test.cpp`.

## See also

- [UI](UI.md)
- [Resources](Resources.md)
- [Asset Codegen](../build/Asset%20Codegen.md)
