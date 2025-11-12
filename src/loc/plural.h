#pragma once

#include <cstdint>
#include <string_view>

namespace engine::loc {

enum class PluralCategory {
    One,
    Few,
    Many,
    Other,
};

// Cardinal category for an integer. The language is the primary subtag (`uk-UA` → `uk`),
// compared case-insensitively. `uk`, `ru`, and `be` share one Slavic rule. Every other tag
// uses English (`n == 1` → one, otherwise other). The sign is ignored; the category follows
// the magnitude.
[[nodiscard]] PluralCategory plural_category(std::string_view locale, std::int64_t n) noexcept;

}
