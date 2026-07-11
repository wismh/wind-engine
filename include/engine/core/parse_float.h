#pragma once

// docs/tech/modules/Core.md

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <string_view>

namespace engine {

// Reads the longest number at the start of `text`: an optional sign, digits, an optional fraction, an optional
// exponent. Returns the characters it used, or 0 when `text` does not start with a number. Hex, `inf`, `nan`, and
// leading blanks are not numbers here.
//
// `std::from_chars` is used where the standard library has it. Apple's libc++ has no floating-point `from_chars`
// before Xcode 16.3, so there it falls back to `strtod` on a copy of the number. `strtod` follows the C locale
// setting, which the engine never changes.
[[nodiscard]] inline std::size_t parse_float_prefix(std::string_view text, double& out) {
    std::size_t length = 0;
    while (length < text.size() && std::string_view("+-0123456789.eE").find(text[length]) != std::string_view::npos) {
        ++length;
    }
    if (length == 0 || length >= 64) {
        return 0;
    }
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    // `from_chars` takes no leading '+'.
    const std::size_t skip = text[0] == '+' ? 1 : 0;
    if (skip == 1 && length > 1 && (text[1] == '+' || text[1] == '-')) {
        return 0;
    }
    double value = 0.0;
    const auto result = std::from_chars(text.data() + skip, text.data() + length, value);
    if (result.ec != std::errc{}) {
        return 0;
    }
    out = value;
    return static_cast<std::size_t>(result.ptr - text.data());
#else
    std::array<char, 64> buffer{};
    text.copy(buffer.data(), length);
    char* end = nullptr;
    const double value = std::strtod(buffer.data(), &end);
    if (end == buffer.data()) {
        return 0;
    }
    out = value;
    return static_cast<std::size_t>(end - buffer.data());
#endif
}

// The whole of `text` is one number.
[[nodiscard]] inline bool parse_float(std::string_view text, double& out) {
    double value = 0.0;
    if (text.empty() || parse_float_prefix(text, value) != text.size()) {
        return false;
    }
    out = value;
    return true;
}

} // namespace engine
