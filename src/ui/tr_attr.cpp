#include "ui/tr_attr.h"

#include <cctype>
#include <optional>

namespace engine::ui {
namespace {

std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

bool is_key_start(unsigned char ch) {
    return std::isalnum(ch) != 0 || ch == '_';
}

bool is_key_cont(unsigned char ch) {
    return std::isalnum(ch) != 0 || ch == '_' || ch == '.' || ch == '-';
}

bool is_name_start(unsigned char ch) {
    return std::isalpha(ch) != 0 || ch == '_';
}

bool is_name_cont(unsigned char ch) {
    return std::isalnum(ch) != 0 || ch == '_';
}

void skip_ws(std::string_view text, std::size_t& index) {
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
        ++index;
    }
}

// `{tr` as its own word, so `{truck}` stays a literal.
bool opens_tr(std::string_view value) {
    constexpr std::string_view kPrefix = "{tr";
    if (!value.starts_with(kPrefix)) {
        return false;
    }
    if (value.size() == kPrefix.size()) {
        return true;
    }
    const unsigned char next = static_cast<unsigned char>(value[kPrefix.size()]);
    return std::isspace(next) != 0 || next == '}';
}

std::optional<std::string> binding_path(std::string_view token) {
    constexpr std::string_view kPrefix = "{binding";
    const std::string_view value = trim(token);
    if (value.size() < kPrefix.size() + 1 || !value.starts_with(kPrefix) || value.back() != '}') {
        return std::nullopt;
    }
    const std::string_view inner = trim(value.substr(kPrefix.size(), value.size() - kPrefix.size() - 1));
    if (inner.empty()) {
        return std::nullopt;
    }
    const auto eq = inner.find('=');
    if (eq != std::string_view::npos) {
        if (trim(inner.substr(0, eq)) == "path") {
            const std::string_view path = trim(inner.substr(eq + 1));
            if (path.empty()) {
                return std::nullopt;
            }
            return std::string(path);
        }
        return std::nullopt;
    }
    return std::string(inner);
}

TrParse error(std::string message) {
    TrParse parsed;
    parsed.kind = TrParse::Kind::Error;
    parsed.message = std::move(message);
    return parsed;
}

}

TrParse parse_tr_attribute(std::string_view raw) {
    const std::string_view value = trim(raw);
    if (!opens_tr(value)) {
        return {};
    }
    if (value.size() < 4 || value.back() != '}') {
        return error("UI {tr} is missing a closing brace");
    }
    const std::string_view body = trim(value.substr(3, value.size() - 4));
    if (body.empty()) {
        return error("UI {tr} is missing a key");
    }

    std::size_t index = 0;
    if (!is_key_start(static_cast<unsigned char>(body[index]))) {
        return error("UI {tr} key is invalid");
    }
    const std::size_t key_begin = index;
    ++index;
    while (index < body.size() && is_key_cont(static_cast<unsigned char>(body[index]))) {
        ++index;
    }

    TrParse parsed;
    parsed.kind = TrParse::Kind::Ok;
    parsed.key = std::string(body.substr(key_begin, index - key_begin));

    while (true) {
        skip_ws(body, index);
        if (index == body.size()) {
            return parsed;
        }
        if (!is_name_start(static_cast<unsigned char>(body[index]))) {
            return error("UI {tr} argument is invalid");
        }
        const std::size_t name_begin = index;
        ++index;
        while (index < body.size() && is_name_cont(static_cast<unsigned char>(body[index]))) {
            ++index;
        }
        const std::string name(body.substr(name_begin, index - name_begin));
        skip_ws(body, index);
        if (index >= body.size() || body[index] != '=') {
            return error("UI {tr} argument is invalid");
        }
        ++index;
        skip_ws(body, index);
        if (index >= body.size() || body[index] != '{') {
            return error("UI {tr} argument must be a {binding} path");
        }
        const std::size_t token_begin = index;
        ++index;
        while (index < body.size() && body[index] != '}') {
            ++index;
        }
        if (index >= body.size()) {
            return error("UI {tr} argument is missing a closing brace");
        }
        ++index;
        auto path = binding_path(body.substr(token_begin, index - token_begin));
        if (!path) {
            return error("UI {tr} argument must be a {binding} path");
        }
        parsed.args.push_back(TrArg{std::move(name), intern(*path)});
    }
}

}
