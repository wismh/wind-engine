#include <engine/project/engine_version.h>

#include <charconv>
#include <system_error>

namespace engine {
namespace {

int stage_rank(char stage) noexcept {
    switch (stage) {
        case 'a':
            return 0;
        case 'b':
            return 1;
        default:
            return 2;
    }
}

// Digits at the front of `text` as a non-negative int, removed from it. False for no digits or an overflow.
bool take_number(std::string_view& text, int& number) {
    std::size_t length = 0;
    while (length < text.size() && text[length] >= '0' && text[length] <= '9') {
        ++length;
    }
    if (length == 0) {
        return false;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + length, number);
    if (error != std::errc{} || end != text.data() + length) {
        return false;
    }
    text.remove_prefix(length);
    return true;
}

bool take_dot(std::string_view& text) {
    if (text.empty() || text.front() != '.') {
        return false;
    }
    text.remove_prefix(1);
    return true;
}

}

std::strong_ordering operator<=>(const EngineVersion& a, const EngineVersion& b) noexcept {
    if (const auto order = a.major <=> b.major; order != 0) {
        return order;
    }
    if (const auto order = a.minor <=> b.minor; order != 0) {
        return order;
    }
    if (const auto order = a.patch <=> b.patch; order != 0) {
        return order;
    }
    if (a.dev != b.dev) {
        return a.dev ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    return stage_rank(a.stage) <=> stage_rank(b.stage);
}

std::optional<EngineVersion> parse_engine_version(std::string_view text) {
    EngineVersion version;
    if (!take_number(text, version.major) || !take_dot(text) || !take_number(text, version.minor) || !take_dot(text) ||
            !take_number(text, version.patch)) {
        return std::nullopt;
    }
    if (!text.empty() && (text.front() == 'a' || text.front() == 'b')) {
        version.stage = text.front();
        text.remove_prefix(1);
    }
    if (text == "-dev") {
        version.dev = true;
        text = {};
    }
    if (!text.empty()) {
        return std::nullopt;
    }
    return version;
}

std::string to_string(const EngineVersion& version) {
    std::string text = std::to_string(version.major) + '.' + std::to_string(version.minor) + '.' +
                       std::to_string(version.patch);
    if (version.stage != 0) {
        text += version.stage;
    }
    if (version.dev) {
        text += "-dev";
    }
    return text;
}

}
