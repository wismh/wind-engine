# UI Basics

Wind includes a declarative UI framework powered by XML templates and a CSS stylesheet engine. The UI integrates directly with ECS through the `UiCanvas` component.

---

## 1. Declarative XML Markup

UI hierarchies are authored as XML documents in `assets/ui/`:

```xml
<!-- assets/ui/hud.xml -->
<Panel class="hud-container">
    <Panel class="top-bar">
        <Text class="title" text="Player Health:"/>
        <Text class="stat-value" text="{healthText}"/>
    </Panel>

    <Button class="btn-pause" command="{togglePause}">
        <Text text="{pauseLabel}"/>
    </Button>
</Panel>
```

### Supported Core Elements:
- `<Panel>`: Flex container (row or column flex direction).
- `<Text>`: Text label supporting `{property}` expressions or localization `{tr key}`.
- `<Button>`: Interactive element binding to an `{ICommand}`.
- `<TextInput>`: Editable text box.
- `<Image>`: Textured sprite element.
- `<ItemsControl>`: Generates list items from an `items_source` collection.
- `<Math>`: LaTeX math rendering element.

---

## 2. CSS Styling

Styles are authored in CSS companion files:

```css
/* assets/ui/hud.css */
.hud-container {
    display: flex;
    flex-direction: column;
    justify-content: space-between;
    width: 100%;
    height: 100%;
    padding: 24px;
}

.top-bar {
    display: flex;
    flex-direction: row;
    gap: 12px;
}

.stat-value {
    color: #44dd88;
    font-size: 20px;
}

.btn-pause {
    background-color: #223344;
    border: 2px solid #557799;
    border-radius: 8px;
    padding: 10px 20px;
}

.btn-pause:hover {
    background-color: #335577;
}
```

---

## 3. Spawning a `UiCanvas`

A `UiCanvas` is a component on an entity in your `World`:

```cpp
#include <engine/ui/canvas.h>
#include <asset_ids.h>

void spawn_hud(engine::ecs::World& world, std::shared_ptr<engine::ui::ViewModel> vm) {
    auto hud_entity = world.create();

    world.emplace<engine::ui::UiCanvas>(hud_entity, engine::ui::UiCanvas{
        .document = assets::ui::hud,           // Cooked AssetId
        .stylesheet = assets::ui::hud_css,     // Cooked AssetId
        .data_context = std::move(vm),         // View model supplying properties
        .fit = engine::ui::UiFit::FillWindow,  // Automatically scales to fill the window
        .order = 100                           // Renders above gameplay
    });
}
```

---

## 4. Canvas Sizing and `UiFit`

- **`UiFit::FillWindow`**: The canvas rect matches the entire window dimensions.
- **`UiFit::ScaleWithScreenSize`**: Lays out based on `reference_size` (e.g. 1920x1080) and scales to match window dimensions uniformly.
- **`UiFit::Fixed`**: Uses an explicit pixel rect.

> [!WARNING]
> When hiding an overlay or modal dialog, switch its fit mode to `UiFit::Fixed` with an empty rect (`render::Rect{}`), or it will intercept mouse pointer clicks even when transparent!

---

## Next Steps

- Connect data and buttons using [MVVM & Data Binding](MVVM-and-Bindings.md).
- Implement interactive panels with [HUD & Controls](HUD-and-Controls.md).
