#include "process/process_command_line.h"

#include <algorithm>
#include <cstddef>

namespace engine {
namespace {

char ascii_lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool same_name(std::string_view a, std::string_view b, bool ignore_case) {
    if (!ignore_case) {
        return a == b;
    }
    return std::ranges::equal(a, b, [](char x, char y) { return ascii_lower(x) == ascii_lower(y); });
}

std::string_view entry_name(std::string_view entry) {
    const std::size_t equals = entry.find('=', 1);
    return equals == std::string_view::npos ? entry : entry.substr(0, equals);
}

}

std::string quote_windows_argument(std::string_view argument) {
    if (!argument.empty() && argument.find_first_of(" \t\n\v\"") == std::string_view::npos) {
        return std::string(argument);
    }
    std::string quoted = "\"";
    std::size_t backslashes = 0;
    for (const char c : argument) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        if (c == '"') {
            quoted.append(backslashes * 2 + 1, '\\');
        } else {
            quoted.append(backslashes, '\\');
        }
        backslashes = 0;
        quoted.push_back(c);
    }
    quoted.append(backslashes * 2, '\\');
    quoted.push_back('"');
    return quoted;
}

std::string windows_command_line(std::string_view program, std::span<const std::string> arguments) {
    std::string line = "\"";
    line.append(program);
    line.push_back('"');
    for (const std::string& argument : arguments) {
        line.push_back(' ');
        line.append(quote_windows_argument(argument));
    }
    return line;
}

std::vector<std::string> merge_environment(
        std::vector<std::string> entries, std::span<const ProcessVariable> set, bool ignore_case) {
    for (const ProcessVariable& variable : set) {
        std::string entry = variable.name + "=" + variable.value;
        const auto found = std::ranges::find_if(entries, [&](const std::string& existing) {
            return same_name(entry_name(existing), variable.name, ignore_case);
        });
        if (found != entries.end()) {
            *found = std::move(entry);
        } else {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

}
