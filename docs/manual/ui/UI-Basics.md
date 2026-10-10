# UI Basics

Wind includes a declarative UI framework powered by XML templates and a CSS stylesheet subset. The UI integrates directly with ECS through the `UiCanvas` component.

---

## 1. Declarative XML Markup

UI hierarchies are authored as XML documents in `assets/ui/`. The root element is always `<Canvas>`. Dynamic values are written `{binding name}`, where `name` is a property or command registered on the view model ([MVVM & Data Binding](MVVM-and-Bindings.md)):

```xml
<!-- assets/ui/hud.xml -->
<Canvas id="hud">
    <Stack class="hud-container" direction="vertical">
        <Stack class="top-bar" direction="horizontal">
            <Label class="title" text="Player Health:"/>
            <Label class="stat-value" text="{binding healthText}"/>
        </Stack>

        <Button class="btn-pause" command="{binding togglePause}" content="Pause"/>
    </Stack>
</Canvas>
```

### Supported Elements
- `<Canvas>`: the document root. Its children are laid over each other in the canvas rect; put a `<Stack>` in it to lay content out.
- `<Stack>`: packs its children in a line. `direction="vertical"` (the default) or `"horizontal"` (`"row"` also works). `gap="8"` sets the spacing in pixels.
- `<ScrollView>`: a vertical `Stack` that scrolls (`overflow-y: auto`). `scroll-x` / `scroll-y` accept a `{binding}` to read and write the scroll position.
- `<Label>`: text, from `text` (a literal, `{binding path}`, or `{tr key}` for a [localized](../assets-and-loc/Localization.md#4-ui-localization-via-tr) string).
- `<Button>`: text from `content` (or `text`) plus `command="{binding name}"`, an `ICommand` on the view model.
- `<Image source="...">`: a picture. `source` is an asset id (32 hex characters) or `{binding path}` to an `AssetId` property, never a file name. The asset is a PNG whose `.meta` says `importer = "ui_image"` (a PNG used only by the UI) or `"texture"`.
- `<Checkbox checked="..." command="{binding name}"/>`: a toggle; `checked` is `true`/`1` or a `{binding}`.
- `<TextInput>`: an editable string bound with `text="{binding name}"`.
- `<ItemsControl items_source="{binding name}">` with an `<ItemTemplate>`: one clone of the template per list item ([HUD & Controls](HUD-and-Controls.md#3-lists-with-itemscontrol)).
- `<Line>`: a segment, styled with `x1`, `y1`, `x2`, `y2`, `stroke`, `stroke-width`.
- `<Viewport>`: a clipped area with its own pan and zoom (`pan-x`, `pan-y`, `zoom` bindings).
- `<Popup>`: a menu shown beside its parent, above every canvas of the window.
- `<Component>`: an empty layout hole.
- `<Math formula="...">`: LaTeX math ([Math Formulae](Math-Formulae.md)).

There is no `<Panel>` or `<Text>`: an unknown tag is a load error. Every element accepts `id`, `class` (space separated), and `name`. A custom style variable is `var-<name>="{binding path}"`. Colors, sizes, and paint come from CSS, not from attributes.

---

## 2. CSS Styling

Styles are authored in CSS files (`importer = "css"`; keep them in their own folder such as `assets/css/`, because `hud.xml` and `hud.css` in one folder would get the same asset id):

```css
/* assets/css/hud.css */
.hud-container {
    width: 100%;
    height: 100%;
    padding: 24px;
    justify-content: space-between;
}

.top-bar {
    gap: 12px;
}

.stat-value {
    color: #44dd88;
    font-size: 20px;
}

.btn-pause {
    background: #223344;
    border-width: 2px;
    border-color: #557799;
    border-radius: 8px;
    padding: 10px 20px;
}

.btn-pause:hover {
    background: #335577;
}
```

The supported subset:

- Selectors: type (`Label`), class (`.c`), id (`#id`), `Label.c`, descendant (`A B`), child (`A > B`). Pseudo-classes `:hover`, `:pressed`, `:disabled`, `:focus`, `:checked`. No `+`, `~`, or comma lists.
- Layout: `width`, `height`, `min-width`, `max-width`, `min-height`, `padding`, `margin`, `gap`, `flex-direction` (`row`/`horizontal`, otherwise vertical), `justify-content` (`start`, `center`, `end`, `space-between`), `align-items`, `position` (`static`, `relative`, `absolute`), `top`, `right`, `bottom`, `left`, `z-index`, `display: none`, `visibility`, `overflow`.
- Paint: `color`, `background` (a color or a gradient), `background-image`, `border-width`, `border-color`, `border-radius`, `opacity`, `transform` (`rotate`, `scale`), `font-size`, `font-family`, `line-height`, `text-align`.
- Motion: `transition` and `@keyframes` / `animation`. `@media (min-width: N)` is supported.
- Units: `px`, `%`, `em`, bare numbers (pixels), and `calc()`. Colors are `#rgb`, `#rrggbb`, or `#rrggbbaa` only.

Things that look like CSS but are not there: `display: flex` (accepted and ignored; a `Stack` is always the layout), `background-color`, the `border` shorthand, and `rgb()`. Styles do not inherit: set `color` and `font-size` on every element that needs them. An unknown property only logs a warning, so check `game.log`. The full list is in [UI](../../tech/modules/UI.md#style).

---

## 3. Spawning a `UiCanvas`

A `UiCanvas` is a component on an entity in your `World`. Hand it the document and stylesheet ids and the view model:

```cpp
#include <engine/ui/canvas.h>
#include <asset_ids.h>

void spawn_hud(engine::ecs::World& world, std::shared_ptr<engine::ui::ViewModel> vm) {
    engine::ecs::Entity hud_entity = world.create();

    world.emplace<engine::ui::UiCanvas>(hud_entity, engine::ui::UiCanvas{
        .document = assets::ui::hud,           // cooked AssetId of assets/ui/hud.xml
        .stylesheet = assets::css::hud,        // cooked AssetId of assets/css/hud.css
        .data_context = std::move(vm),         // view model supplying properties and commands
        .fit = engine::ui::UiFit::FillWindow,  // the canvas is the whole window
        .order = 100                           // drawn above lower orders
    });
}
```

The engine loads the document from `AssetsDb` in its bind phase and binds it to the view model every frame. More stylesheets go in `extra_stylesheets`, and a canvas for another window sets `.window`. The game window already draws UI; you do not need to enable anything.

---

## 4. Canvas Sizing and `UiFit`

- **`UiFit::FillWindow`**: The canvas rect matches the entire window (the default).
- **`UiFit::ScaleWithScreenSize`**: Lays out in a design box given by `reference_size` (e.g. `{1920, 1080}`) and scales it uniformly to fit the window, with letterboxing. Without a positive `reference_size` it behaves like `FillWindow`.
- **`UiFit::Fixed`**: Uses the explicit pixel `rect`.

> [!WARNING]
> A full-window canvas takes the pointer even where nothing is drawn. When hiding an overlay or modal dialog, switch its fit mode to `UiFit::Fixed` with an empty rect (`render::Rect{}`), or it will intercept mouse clicks meant for the canvases and the game below it.

---

## Next Steps

- Connect data and buttons using [MVVM & Data Binding](MVVM-and-Bindings.md).
- Implement interactive panels with [HUD & Controls](HUD-and-Controls.md).
