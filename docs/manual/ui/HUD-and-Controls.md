# HUD & Controls

This chapter outlines best practices for architecting HUDs, modal dialogs, and interactive widgets in Wind.

---

## 1. The HUD Pattern: Methods on the HUD Type

A common anti-pattern is assigning lambda functions to every UI button inside `Game`:

```cpp
// BAD - Clutters Game with button logic and creates messy closure captures
vm.togglePause = [this]() { is_paused_ = !is_paused_; };
vm.restartLevel = [this]() { reload_level(); };
```

### The Recommended Architecture:
Make the HUD / Panel a dedicated class. Panel and button behaviors become member functions on the panel:

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
    GameClock& clock_;
    bool is_open_ = false;
    std::shared_ptr<HudViewModel> vm_;
    engine::ecs::Entity canvas_entity_{};
};
```

Bind commands to member functions:

```cpp
// hud_panel.cpp
HudPanel::HudPanel(engine::ecs::World& world, GameClock& clock)
    : clock_(clock)
    , vm_(std::make_shared<HudViewModel>()) {

    // Assign member function bindings
    vm_->togglePause = [this]() { toggle_pause(); };
    vm_->restartLevel = [this]() { restart_level(); };

    // Spawn canvas
    canvas_entity_ = spawn_canvas(world, ...);
}

void HudPanel::toggle_pause() {
    clock_.paused = !clock_.paused;
}
```

---

## 2. Modal Dialogs and Click-Through

A closed full-window overlay canvas still intercepts pointer events unless its bounds are zeroed:

```cpp
void HudPanel::close() {
    is_open_ = false;
    auto& canvas = world_.get<engine::ui::UiCanvas>(canvas_entity_);
    
    // Set to Fixed with empty rect to prevent swallowing clicks
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

## 3. List Virtualization with `<ItemsControl>`

For large lists of items (inventories, leaderboards, logs), `<ItemsControl>` creates visible rows dynamically:

```xml
<ItemsControl class="inventory-list" items_source="{inventoryItems}">
    <ItemTemplate src="assets/ui/inventory_row.xml"/>
</ItemsControl>
```

In your ViewModel:
```cpp
class InventoryViewModel : public engine::ui::ViewModel {
public:
    engine::ui::BindableList<std::shared_ptr<InventoryRowViewModel>> inventoryItems;
};
```
Wind virtualizes rows with fixed heights automatically, only instantiating DOM nodes for elements currently visible in the viewport.

---

## Next Steps

- Draw custom vector art with [Custom Painting](Custom-Painting.md).
- Display scientific and math formulas with [Math Formulae](Math-Formulae.md).
