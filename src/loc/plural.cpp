#include "loc/plural.h"

#include <cctype>
#include <cstdint>
#include <limits>
#include <string>

namespace engine::loc {
namespace {

std::string language_of(std::string_view locale) {
    const auto cut = locale.find_first_of("-_");
    const std::string_view primary = cut == std::string_view::npos ? locale : locale.substr(0, cut);
    std::string out;
    out.reserve(primary.size());
    for (unsigned char ch : primary) {
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

std::uint64_t magnitude(std::int64_t n) noexcept {
    if (n >= 0) {
        return static_cast<std::uint64_t>(n);
    }
    if (n == std::numeric_limits<std::int64_t>::min()) {
        return static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1ull;
    }
    return static_cast<std::uint64_t>(-n);
}

// CLDR cardinal, integers only: one (1, 21, 31…), few (2–4, 22–24…), many (0, 5–20, 11–14, …).
PluralCategory slavic(std::uint64_t n) noexcept {
    const std::uint64_t mod10 = n % 10ull;
    const std::uint64_t mod100 = n % 100ull;
    if (mod10 == 1ull && mod100 != 11ull) {
        return PluralCategory::One;
    }
    if (mod10 >= 2ull && mod10 <= 4ull && (mod100 < 12ull || mod100 > 14ull)) {
        return PluralCategory::Few;
    }
    return PluralCategory::Many;
}

PluralCategory english(std::uint64_t n) noexcept {
    return n == 1ull ? PluralCategory::One : PluralCategory::Other;
}

}

PluralCategory plural_category(std::string_view locale, std::int64_t n) noexcept {
    const std::uint64_t n_abs = magnitude(n);
    const std::string language = language_of(locale);
    if (language == "uk" || language == "ru" || language == "be") {
        return slavic(n_abs);
    }
    return english(n_abs);
}

}
