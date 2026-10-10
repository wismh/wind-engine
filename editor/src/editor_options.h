#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace editor {

// Command line of wind_editor.
//   --project <dir>   open the project in <dir> (the directory with wind_project.toml). Required: without it the
//                     editor reports that and exits
//   --play            press Play once the editor is up (needs --project)
//   --batch           run one command without a window and exit (needs --project and a command)
//   --export [<dir>]  the batch command: build the project's standalone executable and copy it to <dir>
//                     (<project>/export/<target> without one)
//   --development    with --export: the Development build (the game's DebugGame configuration, with the profiler and
//                     wind-cli) instead of Release
//   --log-file <path> also write the batch output lines to <path>
struct EditorOptions {
    std::optional<std::filesystem::path> project;
    bool play = false;
    bool batch = false;
    // --export was given.
    bool export_game = false;
    // The directory after --export, when there was one.
    std::optional<std::filesystem::path> export_directory;
    // --development was given.
    bool development = false;
    std::optional<std::filesystem::path> log_file;
    // Arguments that were not understood, for one warning line.
    std::vector<std::string> unknown;
};

[[nodiscard]] EditorOptions parse_editor_options(std::span<char* const> args);

// Why these options cannot run, or empty: --batch needs --project and a command, a command needs --batch, and
// --development needs --export.
[[nodiscard]] std::string usage_error(const EditorOptions& options);

}
