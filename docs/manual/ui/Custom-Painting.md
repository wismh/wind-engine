# Custom Painting

When you need vector graphics, gauges, dynamic mini-maps, or custom curves inside your UI without writing shaders, use Wind's `IPaint` and `IDrawList` interfaces.

---

## 1. The `IPaint` Interface

Implement `engine::ui::IPaint` (or wrap a lambda in `engine::ui::RelayPaint`) in your view model or control:

```cpp
#include <engine/ui/draw_list.h>
#include <engine/ui/paint.h>

class HealthBarPainter final : public engine::ui::IPaint {
public:
    float progress = 0.75f; // 75% full

    void paint(engine::ui::IDrawList& draw, const engine::render::Rect& content) override {
        // Background bar
        draw.fill_rect(content, {0.1f, 0.1f, 0.1f, 0.8f}, 4.0f);

        // Filled progress portion
        engine::render::Rect fill = content;
        fill.w *= progress;
        draw.fill_rect(fill, {0.2f, 0.8f, 0.3f, 1.0f}, 4.0f);

        // Outline border
        draw.stroke_rect(content, {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f, 4.0f);
    }
};
```

`engine::render::Rect` is `{x, y, w, h}`. Colors are `glm::vec4` in the 0 to 1 range.

---

## 2. Binding to XML Markup

Give any element a `paint` attribute naming a painter on the view model. The element still gets its CSS size and background; your painting runs after the element's own chrome and before its children, inside its content box:

```xml
<Stack class="health-bar-container" paint="{binding healthPainter}"/>
```

```css
.health-bar-container {
    width: 240px;
    height: 24px;
}
```

In your `ViewModel` register it with `paint()` (or let the generated `bind()` do it, as it does for properties and commands):

```cpp
class HudViewModel : public engine::ui::ViewModel {
public:
    HudViewModel() {
        paint(engine::ui::intern("healthPainter"), healthPainter);
    }

    HealthBarPainter healthPainter;
};
```

Change `healthPainter.progress` from a system as the health changes. By default the painter runs every frame.

### Telling the UI when it changed

The UI keeps what an element paints between frames and replays it while nothing changed. It cannot see inside your painter, so say when it did: override `version()` and return a number that goes up whenever `paint` would draw something different from last time, the same idea as `Bindable::version`:

```cpp
class HealthBarPainter final : public engine::ui::IPaint {
public:
    void set_progress(float value) {
        if (value != progress_) {
            progress_ = value;
            ++version_;
        }
    }

    void paint(engine::ui::IDrawList& draw, const engine::render::Rect& content) override { /* ... */ }
    [[nodiscard]] std::uint64_t version() const noexcept override { return version_; }

private:
    float progress_ = 0.75f;
    std::uint64_t version_ = 1;
};
```

The default `version()` is `IPaint::kVersionUnknown`, which means "may differ every frame": `paint` runs every frame and the UI around it is recorded again each time. A painter that animates by itself can keep that default. A painter that draws the same thing for a long time (a grid, a minimap) should return a version, and `paint` then runs only when it moves. Give the element a `repaint-boundary: yes` rule when the painter changes often, so only that element is recorded again and not the panel it sits in.

---

## 3. Available `IDrawList` Commands

The `IDrawList` interface operates in element-local pixel coordinates (the origin is the top-left of the element's content box):

- `fill_rect(rect, color, radius = 0)`
- `stroke_rect(rect, color, width, radius = 0)`
- `line(from, to, color, width)`
- `arc(center, radius, start_angle, end_angle, color, width)`: a stroked ring segment. Angles are radians, 0 is straight up, increasing clockwise. A live progress ring is one `arc` per frame.
- `set_font(font_id, size)`, then `text(string, position, color)`: the font is the `AssetId` of a font asset (`engine::builtin::font_ui` is the builtin one, from `<engine/builtin_ids.h>`). Call `set_font` before `text`; do not rely on a default font.
- `image(asset_id, rect)`: draws a UI image asset.

There is no `draw_circle`; draw a full circle as an `arc` from `0` to `2 * pi`, or a `fill_rect` with a radius of half the side.

> [!NOTE]
> All drawing commands in `IDrawList` are batched into the engine's 2D vector pass. Game code never invokes the UI renderer or OpenGL functions directly.

---

## Next Steps

- Render mathematical formulas with [Math Formulae](Math-Formulae.md).
- Process input actions in [Action Mapping](../input/Action-Mapping.md).
