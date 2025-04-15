# Localization

Wind includes a built-in localization catalog system supporting multi-language string tables, fallback locales, parameter formatting, and UI `{tr}` integration.

---

## 1. String Tables (`.strings`)

Localization files are authored in TOML format:

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

---

## 2. Setting Up `Catalog` in `on_start()`

The active localization catalog lives in `world.ctx<engine::loc::Catalog>()`:

```cpp
#include <engine/loc/catalog.h>
#include <asset_ids.h>

void MyGame::on_start() {
    auto& catalog = world().ctx<engine::loc::Catalog>();

    // Load string tables from cooked assets
    auto en_table = services_.assets.get<engine::loc::StringTable>(assets::loc::en_strings);
    auto uk_table = services_.assets.get<engine::loc::StringTable>(assets::loc::uk_strings);

    // Register tables (en as baseline Source, uk as Translation)
    catalog.add(*en_table, engine::loc::Role::Source);
    catalog.add(*uk_table, engine::loc::Role::Translation);

    // Set fallback and active locale
    catalog.set_fallback("en");
    catalog.set_active("uk");
}
```

---

## 3. Formatting Text at Runtime

Retrieve translated strings using `catalog.text()`:

```cpp
auto& catalog = world.ctx<engine::loc::Catalog>();

// Simple lookup
engine::loc::Translated play_btn = catalog.text("menu.play");
engine::log::info("Button text: {}", play_btn.text); // "Грати"

// Formatted lookup with arguments
std::array args = {
    engine::loc::Arg{.name = "player", .value = "Alex"},
    engine::loc::Arg{.name = "enemy", .value = "Dragon"}
};
engine::loc::Translated msg = catalog.text("combat.defeat_message", args);
```

---

## 4. UI Localization via `{tr}`

In UI XML templates, use the `{tr key}` syntax:

```xml
<Panel class="main-menu">
    <Button command="{startAction}">
        <Text text="{tr menu.play}"/>
    </Button>
    <Button command="{settingsAction}">
        <Text text="{tr menu.options}"/>
    </Button>
</Panel>
```

When switching the active locale via `catalog.set_active("de")`, UI text elements bound through `{tr}` refresh automatically.

---

## Next Steps

- Package and distribute for [Desktop](../platforms/Desktop.md).
- Export to the browser in [Web (WebAssembly)](../platforms/Web-Wasm.md).
