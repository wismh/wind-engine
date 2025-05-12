#pragma once

#include "sdk_catalog.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace launcher {

// A new project is a copy of the SDK's `templates/empty/` (installed from the engine repo's templates/), so it builds
// against that SDK's API. Every file is text; `{{name}}`, `{{target}}`, `{{engine}}`, and `{{sdk}}` in it are
// replaced. A valid name has no '"' or '\', so it goes into TOML and C++ string literals as is.
struct NewProject {
    // What the user typed: the project's name and its directory's name.
    std::string name;
    // The directory the project directory is made in. Made too when it does not exist.
    std::filesystem::path location;
    std::filesystem::path sdk_root;
    std::string engine;
};

// `<sdk>/templates/empty`.
[[nodiscard]] std::filesystem::path project_template(const SdkEntry& sdk);

// The CMake target for a project name: lower-case ASCII letters and digits, every other run of characters one '-',
// none at either end, "game-" in front of a leading digit. "My Game 2" is "my-game-2". Empty when the name has no
// ASCII letter or digit.
[[nodiscard]] std::string project_target(std::string_view name);

// The longest project directory new_project_problem accepts on Windows, in characters.
inline constexpr std::size_t kMaxProjectPath = 100;

// `location/name`, the directory create_project makes.
[[nodiscard]] std::filesystem::path project_directory(const NewProject& project);

// Why the project cannot be created as it stands, in a sentence, or nullopt. Checks the name (a directory name on
// every platform, with a target), the location (absolute), the project directory's length on Windows, and that the
// directory is missing or empty. Does not look at the SDK.
[[nodiscard]] std::optional<std::string> new_project_problem(const NewProject& project);

// Copies `template_dir` into project_directory(project), replacing the placeholders. On failure removes what it made
// and says why in a sentence. Returns the project directory.
[[nodiscard]] std::expected<std::filesystem::path, std::string> create_project(
        const std::filesystem::path& template_dir, const NewProject& project);

}
