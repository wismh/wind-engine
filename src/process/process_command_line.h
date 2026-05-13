#pragma once

#include <engine/process/process_desc.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

// Windows passes a child one command-line string; the child's C runtime (CommandLineToArgvW rules) cuts it into
// argv again. These build that string so every argument arrives unchanged. UTF-8 in, UTF-8 out.

// `argument` as one argv entry: as is when it has no space, tab, newline, or quote and is not empty, otherwise in
// quotes, with each quote escaped and the backslashes before a quote or the closing quote doubled.
[[nodiscard]] std::string quote_windows_argument(std::string_view argument);

// The program in quotes (argv[0] takes no escapes, and a path holds no quote), then each argument quoted.
[[nodiscard]] std::string windows_command_line(std::string_view program, std::span<const std::string> arguments);

// `entries` ("NAME=value", as the parent's environment lists them) with each of `set` replacing the entry of that
// name or appended after the others. `ignore_case` compares names without ASCII case (Windows). An entry that
// starts with '=' (Windows keeps per-drive directories as "=C:=C:\dir") has its name after that first '='.
[[nodiscard]] std::vector<std::string> merge_environment(
        std::vector<std::string> entries, std::span<const ProcessVariable> set, bool ignore_case);

}
