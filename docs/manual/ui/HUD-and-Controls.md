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
Make the HUD / panel a dedicated class that owns its view model. Each button is a **member function** of the panel, and the panel binds it to the view model's command by member pointer. The engine gives you `ICommand` (`can_execute`, `execute`) and `RelayCommand` (a `std::function` holder); a small member-pointer command is a few lines of your own code:

```cpp
// src/ui/member_command.h (game code)
#pragma once

#include <engine/ui/command.h>

namespace game {

// An ICommand that calls one member function of one owner.
class MemberCommand final : public engine::ui::ICommand {
public:
    template<typename Owner, void (Owner::*Method)()>
    void bind_to(Owner& owner) {
        owner_ = &owner;
        invoke_ = [](void* self) { (static_cast<Owner*>(self)->*Method)(); };
    }

    [[nodiscard]] bool can_execute() const override {
        return invoke_ != nullptr;
    }

    void execute() override {
        if (invoke_ != nullptr) {
            invoke_(owner_);
        }
    }

private:
    void* owner_ = nullptr;
    void (*invoke_)(void*) = nullptr;
};

} // namespace game
```

The view model holds `MemberCommand` members. They are `ICommand`s, so the generated `bind()` registers them like any other command:

```cpp
// hud_view_model.h
class HudViewModel : public engine::ui::ViewModel {
public:
    HudViewModel() {
        assets::ui::Hud::bind(*this);
    }

    MemberCommand togglePause;
    MemberCommand restartLevel;
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

Add and remove rows by changing the list (`inventoryItems.get().push_back(row)` or `inventoryItems.set(rows)`); the next bind pass picks it up.

### Virtualization
A long list does not create a row element per item. When the conditions below hold, the engine builds only the rows near the visible part of the scroller (plus two rows of overscan) and stands in for the rest with empty spacers. The list virtualizes when **all** of these are true:

- the `ItemsControl` stacks vertically (`direction="vertical"`, the default) and has a pixel `gap` or none;
- the template has a single root element;
- the generated row root has a pixel `height` (`height: 28px` or `28`), the same for all rows, from CSS; and
- the list sits in a vertically scrolling container (a `<ScrollView>` ancestor, or the `ItemsControl` itself with `overflow-y: auto`).

If any of them fails, the list simply builds a row for every item. That is correct, just not as fast for thousands of rows.

> [!WARNING]
> Virtualization assumes the rows are stacked in flow. A template root with a CSS `height` makes the list virtualize even when the rows are `position: absolute` (for example, placed by `top`): the engine then computes the visible window from `height` and the scroll offset as if the rows were stacked, builds only the first screenful (a 24-item list showed 9), and the other rows never exist. If you position rows yourself, switch virtualization off by breaking one of the conditions above instead: give the list `flex-direction: row`, and put the pixel `height` on a child element of the template root instead of on the root, or give the root a `%`/`em` height. There is no XML attribute or builder call that turns virtualization off directly (the engine suspends it itself only while a row's `height`, `padding`, `gap`, or `font-size` is animating).

Keep the row height fixed in CSS; rows that change height (wrapped text, for example) do not fit this scheme.

---

## 4. Text Input and Checkboxes

`<TextInput text="{binding query}" command="{binding submit}"/>` edits a `Bindable<std::string>`. The command runs on Enter; the field does not set `disabled` from `can_execute()`. `<Checkbox checked="{binding enabled}" command="{binding toggled}"/>` flips the bound number (`1` or `0`) before running its command. Text the player types is not translated.

---

## Next Steps

- Draw custom vector art with [Custom Painting](Custom-Painting.md).
- Display scientific and math formulas with [Math Formulae](Math-Formulae.md).
