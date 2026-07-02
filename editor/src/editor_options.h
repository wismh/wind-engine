#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace editor {

// Command line of wind_editor.
//   --project <dir>  open the project in <dir> (the directory with wind_project.toml). Required: without it the
//                    editor reports that and exits
//   --play           press Play once the editor is up (needs --project)
struct EditorOptions {
    std::optional<std::filesystem::path> project;
    bool play = false;
    // Arguments that were not understood, for one warning line.
    std::vector<std::string> unknown;
};

[[nodiscard]] EditorOptions parse_editor_options(std::span<char* const> args);

}
