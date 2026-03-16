# Math Formulae

Wind features a built-in mathematical typography engine that renders LaTeX-style mathematical expressions into vector UI elements without external dependencies.

---

## 1. The `<Math>` Element

Use the `<Math>` tag in your UI documents:

```xml
<Panel class="equation-card">
    <Text text="Kinetic Energy:"/>
    <!-- Inline formula -->
    <Math formula="E = \frac{1}{2}mv^2"/>
</Panel>
```

For large display-style equations with centered symbols:

```xml
<Math formula="\int_{0}^{\infty} e^{-x^2} dx = \frac{\sqrt{\pi}}{2}" display="true"/>
```

---

## 2. Dynamic Formula Binding

You can bind the mathematical formula string dynamically from your `ViewModel`:

```xml
<Math formula="{dynamicFormula}"/>
```

```cpp
class PhysicsLabViewModel : public engine::ui::ViewModel {
public:
    engine::ui::Bindable<std::string> dynamicFormula{R"(F = G \frac{m_1 m_2}{r^2})"};
};
```

---

## 3. Supported LaTeX Subset

The built-in parser supports common mathematical notations:
- **Fractions:** `\frac{numerator}{denominator}`
- **Radicals:** `\sqrt{x}`, `\sqrt[n]{x}`
- **Subscripts & Superscripts:** `x_i^2`
- **Vectors & Accents:** `\vec{v}`
- **Operators:** `\sum`, `\int`, `\prod`, `\iint`, `\oint`, `\bigcup`, `\bigcap`
- **Greek Letters:** `\alpha`, `\beta`, `\gamma`, `\theta`, `\pi`, `\omega`
- **Functions:** `\sin`, `\cos`, `\tan`, `\exp`, `\log`, `\ln`, `\lim`, `\max`, `\min`
- **Delimiters:** `\left( ... \right)`, `\left[ ... \right]`

---

## Next Steps

- Set up input handling with [Action Mapping](../input/Action-Mapping.md).
- Process mouse coordinates and clicks with [Raw Input & Mouse](../input/Raw-Input-and-Mouse.md).
