# Localization

Wind includes a built-in localization catalog system supporting multi-language string tables, fallback locales, parameter formatting, and UI `{tr}` integration.

---

## 1. String Tables (`.strings`)

Localization files are authored in TOML, one file per locale:

```toml
# assets/loc/en.strings
locale = "en"

[[string]]
id = "menu.play"
text = "Start Game"

[[string]]
id = "menu.options"
text = "Settings"

[[string]]
id = "combat.defeat_message"
text = "Player {player} was defeated by {enemy}!"
```

```toml
# assets/loc/uk.strings
locale = "uk"

[[string]]
id = "menu.play"
text = "Грати"

[[string]]
id = "menu.options"
text = "Налаштування"

[[string]]
id = "combat.defeat_message"
text = "Гравця {player} було переможено {enemy}!"
```

Each table needs `locale`, and each `[[string]]` needs `id` and `text` (`note` is for translators and is ignored). An id may appear once per table.

Exactly **one** table of the asset tree is the key authority. Mark it in its `.meta` with `source = true`:

```toml
# assets/loc/en.strings.meta
guid = "0c4d5e6f708192a3b4c5d6e7f8091a2b"
importer = "strings"
source = true
```

As soon as the tree has any `.strings` file, `asset_codegen` fails the build unless exactly one table is a source, and it also fails when a `{tr key}` in a UI document is missing from that table.

### Message syntax
- `{name}` inserts an argument (a missing argument stays as `{name}`). `{{` and `}}` are literal braces.
- `{count, plural, one {# file} few {# files} many {# files} other {# files}}` picks a branch by the integer `count`; `#` is the number and `other` is required. `uk`, `ru`, and `be` use the one / few / many rule, every other locale the English one (`1` is `one`).

---

## 2. Setting Up `Catalog` in `on_start()`

The active localization catalog lives in `world.ctx<engine::loc::Catalog>()`. A world that never adds a table works unchanged (keys show as they are). The file `loc/en.strings` has the id `assets::loc::en`:

```cpp
#include <engine/loc/catalog.h>
#include <engine/resources/assets_db.h>
#include <asset_ids.h>

void MyGame::on_start() {
    auto& catalog = world().ctx<engine::loc::Catalog>();

    // Load string tables from cooked assets
    auto en_table = services_.assets.get<engine::loc::StringTable>(assets::loc::en);
    auto uk_table = services_.assets.get<engine::loc::StringTable>(assets::loc::uk);

    // Register tables (en is the source of keys, uk a translation)
    catalog.add(*en_table, engine::loc::Role::Source);
    catalog.add(*uk_table, engine::loc::Role::Translation);

    // Set fallback and active locale
    catalog.set_fallback("en");
    catalog.set_active("uk");
}
```

The engine does not choose or remember the locale: your game picks it (from settings or the OS) and saves it itself.

---

## 3. Formatting Text at Runtime

Retrieve translated strings using `catalog.text()`. The order is the active locale, then the fallback, then the key itself:

```cpp
auto& catalog = world.ctx<engine::loc::Catalog>();

// Simple lookup
engine::loc::Translated play_btn = catalog.text("menu.play");
engine::log::info("Button text: " + play_btn.text);   // "Грати"

// Formatted lookup with arguments (a string_view is not copied: keep it alive for the call)
const std::array args = {
    engine::loc::Arg{.name = "player", .value = std::string_view{"Alex"}},
    engine::loc::Arg{.name = "enemy", .value = std::string_view{"Dragon"}}
};
engine::loc::Translated msg = catalog.text("combat.defeat_message", args);
```

`Translated::missing_from_source` is true when the key is not in the `Role::Source` table. `catalog.set_pseudo(true)` wraps and lengthens every string, which shows clipped layouts and untranslated text.

---

## 4. UI Localization via `{tr}`

In UI XML, `text` and `content` accept `{tr key}`. Keys may contain letters, digits, `_`, `.`, and `-`. Arguments bind to view-model properties: `{tr combat.score points={binding scoreValue}}`.

```xml
<Stack class="main-menu">
    <Button class="menu-button" command="{binding startAction}" content="{tr menu.play}"/>
    <Button class="menu-button" command="{binding settingsAction}" content="{tr menu.options}"/>
</Stack>
```

When you switch the locale with `catalog.set_active("de")`, every label bound through `{tr}` shows the new text on the next frame. An attribute is either `{tr}` or `{binding}`, not both, and `formula` on `<Math>` does not accept `{tr}`. `TextInput` is player text and is not translated.

The builtin UI font (Inter) covers Latin, Greek, and Cyrillic. For a script it lacks, set `font-family` to your own font asset. Text is drawn left to right and is not shaped for Arabic, Hebrew, or Indic scripts.

---

## Next Steps

- Package and distribute for [Desktop](../platforms/Desktop.md).
- Export to the browser in [Web (WebAssembly)](../platforms/Web-Wasm.md).
