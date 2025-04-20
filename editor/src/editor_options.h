#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace editor {

// Command line of wind_editor.
//   --game <module>  use this game module and skip the file dialog at start
//   --play           press Play once the editor is up (needs --game)
struct EditorOptions {
    std::optional<std::filesystem::path> game;
    bool play = false;
    // Arguments that were not understood, for one warning line.
    std::vector<std::string> unknown;
};

[[nodiscard]] EditorOptions parse_editor_options(std::span<char* const> args);

}
