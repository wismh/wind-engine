#pragma once

// docs/tech/modules/Project.md

#include <engine/project/manifest_error.h>

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace engine {

// The file in a game repo's root that makes it a Wind project.
inline constexpr char kWindProjectFile[] = "wind_project.toml";

// wind_project.toml:
//   name = "Tic Tac Toe"     optional, the directory name when absent
//   engine = "0.1.0"         the engine version the project builds against (an SDK's `version`)
//   target = "tic-tac-toe"   the engine_add_game target the editor builds and plays
//   [assets]
//     roots = [...]          optional list of entry point assets (from assets/)
//     always_include = [...] optional list of glob patterns to always include
// Other keys are ignored.
struct WindProject {
    std::string name;
    std::string engine;
    std::string target;
    std::vector<std::string> asset_roots;
    std::vector<std::string> asset_always_include;
};

// Reads `<directory>/wind_project.toml`.
[[nodiscard]] std::expected<WindProject, ManifestFailure> read_wind_project(const std::filesystem::path& directory);

}
