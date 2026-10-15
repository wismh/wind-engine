# HUD & Controls

This chapter outlines best practices for architecting HUDs, modal dialogs, and interactive widgets in Wind.

---

## 1. The HUD Pattern: Methods on the HUD Type

A common anti-pattern is assigning a lambda to every UI button inside `Game`:

```cpp
// BAD - Clutters Game with button logic and creates messy closure captures
vm->togglePause = [this]() { is_paused_ = !is_paused_; };
vm->restartLevel = [this]() { reload_level(); };
```

### The Recommended Architecture
Make the HUD / panel a dedicated class that owns its view model. Each button is a **member function** of the panel, and the panel binds it to the view model's command by member pointer. The engine gives you `ICommand` (`can_execute`, `execute`), `RelayCommand` (a `std::function` holder, for tests and quick scripts), and `MethodCommand`, which holds a pointer to the owner and a plain function pointer: no `std::function`, no allocation. The view model exists before its owner, so `MethodCommand` is bound after construction with `bind_to<Owner, &Owner::method>(owner)`. Until then it does nothing and `can_execute()` is `false`. Calling `bind_to` again moves the command to a new owner. A second method pointer, `bind_to<Owner, &Owner::method, &Owner::can_method>(owner)`, takes a `bool can_method() const` that decides `can_execute()`:

The view model holds `engine::ui::MethodCommand` members. They are `ICommand`s, so the generated `bind()` registers them like any other command:

```cpp
// hud_view_model.h
class HudViewModel : public engine::ui::ViewModel {
public:
    HudViewModel() {
        assets::ui::Hud::bind(*this);
    }

    engine::ui::MethodCommand togglePause;
    engine::ui::MethodCommand restartLevel;
};
```

```cpp
// hud_panel.h
class HudPanel {
public:
    HudPanel(engine::ecs::World& world, GameClock& clock);

    void toggle_pause();
    void restart_level();

    [[nodiscard]] bool is_open() const { return is_open_; }
    void open();
    void close();

private:
    engine::ecs::World& world_;
    GameClock& clock_;
    bool is_open_ = false;
    std::shared_ptr<HudViewModel> vm_;
    engine::ecs::Entity canvas_entity_{};
};
```

```cpp
// hud_panel.cpp
HudPanel::HudPanel(engine::ecs::World& world, GameClock& clock)
    : world_(world)
    , clock_(clock)
    , vm_(std::make_shared<HudViewModel>()) {

    vm_->togglePause.bind_to<HudPanel, &HudPanel::toggle_pause>(*this);
    vm_->restartLevel.bind_to<HudPanel, &HudPanel::restart_level>(*this);

    canvas_entity_ = world_.create();
    world_.emplace<engine::ui::UiCanvas>(canvas_entity_, engine::ui::UiCanvas{
        .document = assets::ui::hud,
        .stylesheet = assets::css::hud,
        .data_context = vm_,
        .order = 100,
    });
}

void HudPanel::toggle_pause() {
    clock_.paused = !clock_.paused;
}
```

The `Game` creates the panel in `on_start` (`hud_.emplace(world(), clock_)`) and then calls it directly, with no null checks ([Game Lifecycle](../architecture/Game-Lifecycle.md#5-architectural-rule-deferred-optional-initialization)). Destroy the canvas entity in the panel's destructor (`world_.destroy(canvas_entity_)`): the canvas shares ownership of the view model, whose commands point back at the panel.

---

## 2. Modal Dialogs and Click-Through

A closed full-window overlay canvas still intercepts pointer events, even over empty space, unless its bounds are empty:

```cpp
void HudPanel::close() {
    is_open_ = false;
    auto& canvas = world_.get<engine::ui::UiCanvas>(canvas_entity_);

    // Fixed with an empty rect: the canvas no longer swallows clicks
    canvas.fit = engine::ui::UiFit::Fixed;
    canvas.rect = engine::render::Rect{};
}

void HudPanel::open() {
    is_open_ = true;
    auto& canvas = world_.get<engine::ui::UiCanvas>(canvas_entity_);

    // Restore window-filling bounds
    canvas.fit = engine::ui::UiFit::FillWindow;
}
```

---

## 3. Lists with `<ItemsControl>`

For lists (inventories, leaderboards, logs), `<ItemsControl>` makes one row per element of a view-model list. The row layout is an `<ItemTemplate>`, inline or included from another XML file (`src` is a path relative to the including file):

```xml
<!-- assets/ui/inventory.xml (inside its <Canvas> root) -->
<ScrollView class="inventory-scroll">
    <ItemsControl class="inventory-list" items_source="{binding inventoryItems}">
        <ItemTemplate>
            <Stack class="inventory-row" direction="horizontal">
                <Label class="row-name" text="{binding name}"/>
                <Label class="row-count" text="{binding count}"/>
            </Stack>
        </ItemTemplate>
    </ItemsControl>
</ScrollView>
```

```xml
<ItemsControl class="inventory-list" items_source="{binding inventoryItems}">
    <ItemTemplate src="inventory_row.xml"/>   <!-- assets/ui/inventory_row.xml, next to this document -->
</ItemsControl>
```

The bindings inside the template resolve against each row's view model, which is also a `ViewModel`:

```cpp
class InventoryRowViewModel : public engine::ui::ViewModel {
public:
    InventoryRowViewModel() {
        assets::ui::Inventory::InventoryItems::bind(*this);   // the nested binder of the list
    }

    engine::ui::Bindable<std::string> name;
    engine::ui::Bindable<int> count;
};

class InventoryViewModel : public engine::ui::ViewModel {
public:
    InventoryViewModel() {
        assets::ui::Inventory::bind(*this);
    }

    engine::ui::BindableList<std::shared_ptr<InventoryRowViewModel>> inventoryItems;
};
```

Add and remove rows by changing the list with `inventoryItems.set(rows)` or `inventoryItems.edit([&](auto& rows) { rows.push_back(row); })`; the next bind pass picks it up. `get()` returns a `const` vector: a change the list does not see could not be noticed, so every change goes through `set` or `edit`, and the list counts a change (`version()`) only when its items really differ afterwards.

### Virtualization
A long list does not create a row element per item. When the conditions below hold, the engine builds only the rows near the visible part of the scroller (plus two rows of overscan) and stands in for the rest with empty spacers. The list virtualizes when **all** of these are true:

- the `ItemsControl` stacks vertically (`direction="vertical"`, the default) and has a pixel `gap` or none;
- the template has a single root element;
- the rows are in flow (not `position: absolute`); and
- the list sits in a vertically scrolling container (a `<ScrollView>` ancestor, or the `ItemsControl` itself with `overflow-y: auto`).

If any of them fails, the list simply builds a row for every item. That is correct, just not as fast for thousands of rows.

> [!NOTE]
> Virtualization assumes the rows are stacked in flow, so a list whose rows are `position: absolute` (placed by `left` / `top`, see the next section) is never virtualized: every row is built, and a 24-item list shows 24 items. This holds from the second frame; the first frame builds every row anyway. If rows that are in flow must not virtualize either, break one of the other conditions: give the list `flex-direction: row`, or do not put it in a scrolling container. There is no XML attribute or builder call that turns virtualization off directly (the engine suspends it itself only while a row's `height`, `padding`, `gap`, or `font-size` is animating).

Rows may have a fixed pixel `height` (`height: 28px`, the same for all rows) or no fixed height at all (a label that wraps, rows of different content). With a fixed height the whole list is `N * height`. Without one, the engine builds every row on the first frame, measures them, and from the next frame builds only a window; a row it has not measured yet counts as the average of the others, so the scrollbar length is an estimate that settles as you scroll. The scroll position is not corrected when a row above the window turns out taller or shorter than its estimate.

### Items placed by position, and dragged

An inventory, a board, or a set of markers is a list of view models whose items sit at their own position. Every binding in the template resolves against the row's view model, so each item brings its own `x`, `y`, order, and drag value:

```xml
<Stack class="board">
    <ItemsControl class="slots" items_source="{binding slots}">
        <ItemTemplate>
            <Stack class="slot" var-x="{binding x}" var-y="{binding y}" var-z="{binding z}" drag="{binding fraction}"/>
        </ItemTemplate>
    </ItemsControl>
</Stack>
```

```css
.board { position: relative; width: 300; height: 200; }
.slots { width: 300; height: 200; }
.slot { position: absolute; left: var(--x); top: var(--y); width: 40; height: 40; z-index: var(--z); }
```

```cpp
class SlotViewModel : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<float> x;          // pixels, no "12.5px" string
    engine::ui::Bindable<float> y;
    engine::ui::Bindable<int> z;            // higher is in front, for painting and for the pointer
    engine::ui::Bindable<float> fraction;   // 0..1 along the slot while it is dragged
};
```

The items are placed against the nearest `position: relative` or `absolute` ancestor (`.board`). Moving one slot is `slot->x.set(...)`; the others do not move. A drag writes only the dragged slot's own `fraction`, and `on_changed` tells you ([MVVM & Bindings](MVVM-and-Bindings.md#3-reacting-to-changes)). Adding and removing slots leaves the other slots' bindables and drags alone. A name a slot's view model does not register is logged once per slot. Putting the slot at the pointer while it is dragged is your code: read the pointer from the drag fraction, or from `MouseEvent`, and set `x` / `y`.

---

## 4. Sliders

`<Slider value="{binding level}" min="0" max="100" step="5"/>` writes a number in its range into a `Bindable<float>` while the player drags, and shows that number back by placing its handle. It is built on the `drag` machinery, so it works inside an `ItemsControl` row (each row has its own value), keeps tracking when the pointer leaves the track, and `on_changed` fires once per step.

```xml
<Slider class="volume" value="{binding volume}" min="0" max="100" step="5"/>
<Slider class="gain" value="{binding gain}" min="-12" max="12" drag-orientation="vertical"/>
```

The track is the slider itself, styled by your class. The engine adds two parts, `slider-fill` and `slider-handle`, and sets the custom property `--fraction` (0 to 1) on the slider and on both parts. You place them in CSS:

```css
.volume { width: 200; height: 20; background: #222; }
.volume .slider-handle { --handle: 20px; position: absolute; top: 0; width: var(--handle); height: 20;
                         left: calc(var(--fraction) * (100% - var(--handle))); background: #ccc; }
.volume .slider-fill { position: absolute; left: 0; top: 0; height: 20; width: calc(var(--fraction) * 100%); background: #58a; }
```

The handle's width is written once, in CSS. The engine reads the laid-out handle to map the pointer to the value, so the handle's centre sits under the pointer and both ends reach `min` and `max`; there is no pixel constant to keep in step in C++. A vertical slider grows upward (`bottom: calc(var(--fraction) * (100% - var(--handle)))` for the handle). With `step`, the value is a multiple of it counted from `min`. A missing `value` binding, or one that is not a number, is logged by name.

---

## 5. Text Input and Checkboxes

`<TextInput text="{binding query}" command="{binding submit}"/>` edits a `Bindable<std::string>`. The command runs on Enter; the field does not set `disabled` from `can_execute()`. `<Checkbox checked="{binding enabled}" command="{binding toggled}"/>` flips the bound number (`1` or `0`) before running its command. Text the player types is not translated.

---

## Next Steps

- Draw custom vector art with [Custom Painting](Custom-Painting.md).
- Display scientific and math formulas with [Math Formulae](Math-Formulae.md).
