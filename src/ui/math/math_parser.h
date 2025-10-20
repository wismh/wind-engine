#pragma once

#include "ui/math/math_ast.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::ui::math {

enum class ParseErrorKind {
    UnknownCommand,
    UnexpectedCharacter,
    MissingClosingBrace,      // `{` (or `[` of `\sqrt[`) never closed
    UnexpectedClosingBrace,   // `}` with no open group
    MissingArgument,
    MissingDelimiter,         // `\left` / `\right` not followed by a delimiter
    UnmatchedLeft,
    UnmatchedRight,
    DoubleScript,             // two `^` (or two `_`) on one base
    LimitsOnNonOperator,
    InvalidUtf8,
    TooDeep,
};

struct ParseError {
    ParseErrorKind kind = ParseErrorKind::UnexpectedCharacter;
    std::size_t offset = 0;  // byte offset into the source
    std::string detail;      // the command name for UnknownCommand, else empty
};

// Parsing never fails outright: `root` is always something layout can draw (a rejected command comes
// out as its own source text, a missing `}` closes at the end, ...), and `errors` lists everything that
// was off so the caller can warn once and decide whether to show the formula at all.
struct ParseResult {
    Row root;
    std::vector<ParseError> errors;

    [[nodiscard]] bool ok() const noexcept {
        return errors.empty();
    }
};

// Parses the supported TeX subset: letters/digits/operators, `{}` groups, `^` `_` and `'`, `\frac`,
// `\sqrt[n]{}`, large operators with `\limits`/`\nolimits`, `\left...\right`, Greek letters, relation /
// binary / arrow symbols, `\text{}` / `\mathrm{}` / `\operatorname{}`, named functions (`\sin`, `\lim`,
// ...), and spacing (`\,` `\:` `\;` `\!` `\quad` `\qquad`). `source` is UTF-8.
[[nodiscard]] ParseResult parse_formula(std::string_view source);

[[nodiscard]] std::string_view to_string(ParseErrorKind kind) noexcept;

// One-line diagnostic for logs, e.g. `unknown command \foo at byte 3`.
[[nodiscard]] std::string describe(const ParseError& error);

// The command tables, exposed so tests can check every emitted code point against the real font.
struct CommandSymbol {
    std::string_view name;  // without the backslash
    Symbol symbol;
};

[[nodiscard]] std::span<const CommandSymbol> command_symbols();

struct FunctionName {
    std::string_view name;  // `sin`, `lim`, ...; rendered upright as-is
    bool limits_in_display = false;
};

[[nodiscard]] std::span<const FunctionName> function_names();

}
