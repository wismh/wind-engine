# Custom Painting

When you need vector graphics, gauges, dynamic mini-maps, or custom curves inside your UI without writing shaders, use Wind's `IPaint` and `IDrawList` interfaces.

---

## 1. The `IPaint` Interface

Implement `engine::ui::IPaint` in your view model or control:

```cpp
#include <engine/ui/paint.h>
#include <engine/ui/draw_list.h>

class HealthBarPainter : public engine::ui::IPaint {
public:
    float progress = 0.75f; // 75% full

    void paint(engine::ui::IDrawList& draw, const engine::render::Rect& bounds) override {
        // Draw background bar
        draw.fill_rect(bounds, {0.1f, 0.1f, 0.1f, 0.8f});

        // Draw filled progress portion
        engine::render::Rect fill_rect = bounds;
        fill_rect.width *= progress;
        draw.fill_rect(fill_rect, {0.2f, 0.8f, 0.3f, 1.0f});

        // Draw outline border
        draw.stroke_rect(bounds, {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f);
    }
};
```

---

## 2. Binding to XML Markup

In your XML markup, add the `paint` attribute to a `<Panel>`:

```xml
<Panel class="health-bar-container" paint="{healthPainter}"/>
```

In your `ViewModel`:

```cpp
class HudViewModel : public engine::ui::ViewModel {
public:
    HudViewModel() {
        paint(engine::ui::intern("healthPainter"), healthPainter);
    }

    HealthBarPainter healthPainter;
};
```

---

## 3. Available `IDrawList` Commands

The `IDrawList` interface operates in element-local pixel coordinates:
- `fill_rect(rect, color, radius)`
- `stroke_rect(rect, color, stroke_width, radius)`
- `draw_line(p1, p2, color, stroke_width)`
- `draw_circle(center, radius, color)`
- `draw_text(text, position, font_id, size, color)`
- `draw_image(asset_id, rect, tint)`

> [!NOTE]
> All drawing commands in `IDrawList` are batched into the engine's 2D vector pass. Game code never invokes NanoVG or OpenGL functions directly.

---

## Next Steps

- Render mathematical formulas with [Math Formulae](Math-Formulae.md).
- Process input actions in [Action Mapping](../input/Action-Mapping.md).
