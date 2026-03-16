# Common Pitfalls

Review these common developer mistakes before writing or reviewing code in a Wind game.

---

## 1. Optional Means "Deferred Initialization", Not "Maybe Null"

`std::optional` is often used to hold types created in `on_start()`. Once `on_start()` completes, that object is guaranteed to be present for the remainder of the application lifecycle.

```cpp
// BAD — Noise checks that hide actual logic bugs
if (hud_) {
    hud_->apply_locale();
}

// GOOD — Call directly; check real domain state if needed
hud_->apply_locale();
if (hud_->is_open()) {
    hud_->close();
}
```

---

## 2. Never Attach UI Button Lambdas to `Game`

Do not dump every button click lambda onto `Game::bind_panel_commands`:

```cpp
// BAD
class Game {
    void on_start() {
        vm_->btnA = [this]() { do_a(); };
        vm_->btnB = [this]() { do_b(); };
        vm_->btnC = [this]() { do_c(); };
    }
};
```

**Good Practice:** The HUD/Panel owns its view model and button callbacks. Buttons are member functions on the panel class (e.g. `Panel::on_click_play()`).

---

## 3. Compare `ActionId`, Never Physical `KeyCode`

```cpp
// BAD
if (key_event.key == engine::KeyCode::Space) {
    jump();
}

// GOOD
if (input_event.action == action_jump_) {
    jump();
}
```

Binding physical keys to named actions enables input remapping, multi-device support (gamepad/keyboard), and user customization without rewriting gameplay code.

---

## 4. Key Auto-Repeat & Mouse Dragging Stay on Raw Events

- `InputSystem` suppresses key repeats for `InputEvent`.
- If a mechanic requires continuous scrub while held (e.g., holding Right Arrow to scrub frames), read raw `engine::KeyEvent` and inspect `k.repeat`.
- Mouse panning, dragging, and cursor position tracking stay on `engine::MouseEvent`.

---

## 5. Overlay Canvases Must Zero Their Bounds When Closed

A full-window canvas overlay with `UiFit::FillWindow` intercepts clicks even if all its elements are transparent or hidden!

When closing an overlay:
```cpp
canvas.fit = engine::ui::UiFit::Fixed;
canvas.rect = engine::render::Rect{}; // Empty rect
```
When opening the overlay:
```cpp
canvas.fit = engine::ui::UiFit::FillWindow;
```

---

## 6. Wrap Shaders in CDATA

Because `.shader` files are parsed as XML, GLSL relational operators (`<`, `<=`) will break XML parsing silently:

```xml
<!-- BAD: Fails silently -->
<vertex>
    for (int i = 0; i < 4; ++i) { ... }
</vertex>

<!-- GOOD: Wrapped in CDATA with no whitespace after tag -->
<vertex><![CDATA[
    for (int i = 0; i < 4; ++i) { ... }
]]></vertex>
```

---

## 7. Never Rename `user_data_directory` Parameters

`user_data_directory("MyStudio", "MyGame")` uses the organization and application names as the persistent folder identifier. If you rename either string, existing player saves will not be discovered.

---

## Next Steps

- Learn how to structure tests in [Testing Game Code](Testing-Game-Code.md).
- Return to the [Quickstart](../getting-started/Quickstart.md) to begin building.
