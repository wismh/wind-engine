# Math Formulae

Wind features a built-in mathematical typography engine that renders LaTeX-style mathematical expressions into vector UI elements without external dependencies.

---

## 1. The `<Math>` Element

Use the `<Math>` tag in your UI documents:

```xml
<Stack class="equation-card" direction="vertical">
    <Label text="Kinetic Energy:"/>
    <!-- Inline style formula -->
    <Math formula="E = \frac{1}{2}mv^2"/>
</Stack>
```

For display-style equations (a `\sum` or `\prod` puts its limits above and below, integrals keep theirs at the side), set `display="true"`:

```xml
<Math formula="\int_{0}^{\infty} e^{-x^2} dx = \frac{\sqrt{\pi}}{2}" display="true"/>
```

XML rules apply to the attribute: write `<` as `&lt;` and `&` as `&amp;`. The formula is notation, so it cannot be a `{tr}` string.

A formula can also sit inside the text of a `Label` or `Button` between `\(` and `\)`: `<Label text="Energy is \(E = mc^2\) in vacuum"/>`. It is one unbreakable box on the text row. Write `\\(` for a literal `\(`.

---

## 2. Dynamic Formula Binding

You can bind the formula string from your `ViewModel`:

```xml
<Math formula="{binding dynamicFormula}"/>
```

```cpp
class PhysicsLabViewModel : public engine::ui::ViewModel {
public:
    PhysicsLabViewModel() {
        property(engine::ui::intern("dynamicFormula"), dynamicFormula);
    }

    engine::ui::Bindable<std::string> dynamicFormula{R"(F = G \frac{m_1 m_2}{r^2})"};
};
```

---

## 3. Supported LaTeX Subset

The built-in parser supports common mathematical notation:
- **Letters and digits:** a Latin letter is set in math italic, digits upright.
- **Fractions and roots:** `\frac{numerator}{denominator}`, `\sqrt{x}`, `\sqrt[n]{x}`
- **Subscripts, superscripts, primes:** `x_i^2`, `f'`
- **Accents:** `\vec{v}` (the only accent)
- **Large operators:** `\sum`, `\prod`, `\coprod`, `\int`, `\iint`, `\iiint`, `\oint`, `\bigcup`, `\bigcap`, `\bigvee`, `\bigwedge`, `\bigoplus`, `\bigotimes`, with `\limits` / `\nolimits`
- **Greek letters:** `\alpha`, `\beta`, `\gamma`, `\theta`, `\pi`, `\omega`, and the rest (lowercase italic, uppercase upright), plus relation, binary, and arrow symbols
- **Functions (set upright):** `\sin`, `\cos`, `\tan`, `\exp`, `\log`, `\ln`, `\lim`, `\max`, `\min`, `\det`, `\gcd`, and similar
- **Text:** `\text{}`, `\mathrm{}`, `\operatorname{}`
- **Delimiters:** `\left( ... \right)`, `\left[ ... \right]`
- **Spacing:** `\,`, `\:`, `\;`, `\!`, `\quad`, `\qquad`

A command the parser does not know does not fail the document: it is drawn as its source text. The math font loads the first time a formula is drawn.

---

## Next Steps

- Set up input handling with [Action Mapping](../input/Action-Mapping.md).
- Process mouse coordinates and clicks with [Raw Input & Mouse](../input/Raw-Input-and-Mouse.md).
