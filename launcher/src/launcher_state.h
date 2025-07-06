#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace launcher {

// What the launcher remembers between runs: the projects, most recently opened first, and the SDKs the user
// located by hand (SDKs under the install directory are found without being remembered).
//
// The file is UTF-8 text, one entry per line: `project=<path>` or `sdk=<path>`. Blank lines, `#` comments, and
// lines the launcher does not know are skipped, so an older launcher reads a newer file.
struct LauncherState {
    std::vector<std::filesystem::path> projects;
    std::vector<std::filesystem::path> sdks;
};

[[nodiscard]] LauncherState parse_launcher_state(std::string_view text);
[[nodiscard]] std::string format_launcher_state(const LauncherState& state);

// A missing or unreadable file is an empty state.
[[nodiscard]] LauncherState load_launcher_state(const std::filesystem::path& file);
// False when the file cannot be written.
[[nodiscard]] bool save_launcher_state(const std::filesystem::path& file, const LauncherState& state);

// Puts `directory` first, removing an earlier entry for the same directory.
void remember_project(LauncherState& state, const std::filesystem::path& directory);
void forget_project(LauncherState& state, const std::filesystem::path& directory);
// Adds `root` at the end unless it is there already.
void remember_sdk(LauncherState& state, const std::filesystem::path& root);
void forget_sdk(LauncherState& state, const std::filesystem::path& root);

// The same directory: equal after making both absolute and lexically normal, without case on Windows.
[[nodiscard]] bool same_directory(const std::filesystem::path& a, const std::filesystem::path& b);

// UTF-8 text of a path, with '/' separators.
[[nodiscard]] std::string path_text(const std::filesystem::path& path);

}
