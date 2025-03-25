#pragma once

#include <engine/loc/catalog.h>

#include <expected>
#include <string>
#include <string_view>

namespace engine::loc {

enum class FormatError {
    Unclosed,
    BadName,
    NestedPlural,
    MissingOther,
    DuplicateCategory,
    BadPlural,
};

// `{name}` substitutes an arg. `{count, plural, one {…} other {…}}` picks a branch from
// `plural_category`. `#` inside a branch is that integer. `{{` is a literal `{`. `}}` is a
// literal `}` outside a branch; inside a branch the first `}` closes it. A plural nested
// inside a branch is an error. Every plural must include `other`.
// A missing arg is left as `{name}`. The plural locale is the language of the pattern, not
// the language the arg was authored in.
[[nodiscard]] std::expected<void, FormatError> validate_pattern(std::string_view pattern);
[[nodiscard]] std::expected<std::string, FormatError> format(std::string_view pattern, std::span<const Arg> args,
        std::string_view locale);

}
