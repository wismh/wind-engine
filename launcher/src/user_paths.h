#pragma once

#include <filesystem>

namespace launcher {

// An environment variable as a path (UTF-16 on Windows, so any user name works), empty when it is not set.
[[nodiscard]] std::filesystem::path environment_path(const char* name);

// Where a new project goes unless the user picked another place before: `%USERPROFILE%/WindProjects` on Windows,
// `$HOME/WindProjects` elsewhere. Empty when that variable is not set.
[[nodiscard]] std::filesystem::path default_project_location();

}
