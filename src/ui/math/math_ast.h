#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace engine::ui::math {

// TeX atom classes: the layout stage picks inter-atom spacing from these (a Bin gets medium space
// either side, a Rel thick space, an Op thin space, ...). Not every class needs a distinct glyph.
enum class MathClass {
    Ord,
    Op,     // large operator (sum, integral, ...) — may take limits
    Bin,
    Rel,
    Open,
    Close,
    Punct,
};

// `\limits` / `\nolimits` after a large operator or an operator name. Default leaves the choice to
// layout: limits above/below in display style for operators that take them (sum, lim), beside the
// operator otherwise (integrals).
enum class LimitsMode {
    Default,
    Limits,
    NoLimits,
};

struct Node;
using Row = std::vector<Node>;

// One glyph. `codepoint` is what layout looks up in the font: Latin and lower-case Greek letters are
// already mapped to their Mathematical Italic code points, upper-case Greek and digits stay upright.
struct Symbol {
    char32_t codepoint = 0;
    MathClass math_class = MathClass::Ord;
    // Op only: whether display style puts sub/superscripts above and below (sum, product) instead of
    // beside (integrals).
    bool limits_in_display = false;
};

// Upright text: `\text{...}`, `\mathrm{...}`, and the fallback for a command the parser rejected.
struct Text {
    std::u32string text;
};

// `\sin`, `\lim`, `\operatorname{...}`: an upright name that behaves as an operator for spacing.
struct OperatorName {
    std::u32string name;
    bool limits_in_display = false;
};

// Horizontal space in em; negative for `\!`.
struct Space {
    float em = 0.0f;
};

// `{...}`: a sub-row laid out as one unit (one atom for the purpose of scripts).
struct Group {
    Row items;
};

struct Fraction {
    Row numerator;
    Row denominator;
};

struct Radical {
    Row radicand;
    std::optional<Row> index;
};

// `\vec x`: a combining mark (U+20D7 for `\vec`) centred over its base, at the base's MATH top-accent
// attachment point when the base is a lone glyph and at the middle of the box otherwise.
struct Accent {
    char32_t mark = 0;
    Row base;
};

// `base^sup_sub`. `base` is exactly one node (a Symbol, a Group, ...) — an empty Group for `^2` with
// nothing before it. At least one of `subscript` / `superscript` is set.
struct Scripts {
    Row base;
    std::optional<Row> subscript;
    std::optional<Row> superscript;
    LimitsMode limits = LimitsMode::Default;
};

// `\left<open> body \right<close>`. 0 stands for the null delimiter `.`.
struct Delimited {
    char32_t open = 0;
    char32_t close = 0;
    Row body;
};

struct Node {
    std::variant<Symbol, Text, OperatorName, Space, Group, Fraction, Radical, Accent, Scripts, Delimited> value;
};

}
