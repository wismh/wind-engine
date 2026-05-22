#pragma once

#include <engine/process/process_desc.h>

#include <string>
#include <string_view>
#include <vector>

namespace editor {

// What cmake needs on this machine, beyond the editor's own environment. `problem` empty means a build can start.
// On Windows the editor is not launched from a developer prompt, so a Ninja build still has to see `cl` and Ninja.
struct NativeBuild {
    std::vector<engine::ProcessVariable> environment;
    std::string problem;
};

// `NAME=value` lines from `cmd /u /c set`, as UTF-8. A line whose name is empty or starts with `=` is skipped
// (cmd's `=C:=...` drive entry). The value keeps every `=` after the first.
[[nodiscard]] std::vector<engine::ProcessVariable> parse_environment_dump(std::string_view text);

// The MSVC developer environment and Ninja, loaded once per process. Elsewhere: an empty build, because the
// compiler and Ninja are already on PATH. A failure is `problem` and an empty environment.
[[nodiscard]] const NativeBuild& native_build();

}
