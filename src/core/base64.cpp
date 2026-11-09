#include "base64.h"

#include <array>

namespace engine {
namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

[[nodiscard]] int value_of(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

}

std::string base64_encode(std::span<const std::uint8_t> bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned b0 = bytes[i];
        const unsigned b1 = i + 1 < bytes.size() ? bytes[i + 1] : 0u;
        const unsigned b2 = i + 2 < bytes.size() ? bytes[i + 2] : 0u;
        const unsigned triple = (b0 << 16) | (b1 << 8) | b2;
        out += kAlphabet[(triple >> 18) & 63u];
        out += kAlphabet[(triple >> 12) & 63u];
        out += i + 1 < bytes.size() ? kAlphabet[(triple >> 6) & 63u] : '=';
        out += i + 2 < bytes.size() ? kAlphabet[triple & 63u] : '=';
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> base64_decode(std::string_view text) {
    if (text.size() % 4 != 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const bool last = i + 4 == text.size();
        int padding = 0;
        std::array<int, 4> values{};
        for (std::size_t k = 0; k < 4; ++k) {
            const char c = text[i + k];
            if (c == '=') {
                // Only the last one or two characters of the last group may be padding.
                if (!last || k < 2) {
                    return std::nullopt;
                }
                ++padding;
                values[k] = 0;
                continue;
            }
            if (padding > 0) {
                return std::nullopt;
            }
            values[k] = value_of(c);
            if (values[k] < 0) {
                return std::nullopt;
            }
        }
        const unsigned triple = (static_cast<unsigned>(values[0]) << 18) | (static_cast<unsigned>(values[1]) << 12) |
                                (static_cast<unsigned>(values[2]) << 6) | static_cast<unsigned>(values[3]);
        out.push_back(static_cast<std::uint8_t>((triple >> 16) & 0xFFu));
        if (padding < 2) {
            out.push_back(static_cast<std::uint8_t>((triple >> 8) & 0xFFu));
        }
        if (padding < 1) {
            out.push_back(static_cast<std::uint8_t>(triple & 0xFFu));
        }
    }
    return out;
}

}
