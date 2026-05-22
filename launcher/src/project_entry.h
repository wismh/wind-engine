#pragma once

#include <engine/project/wind_project.h>

#include <filesystem>
#include <optional>
#include <string>

namespace launcher {

// One remembered project as the launcher shows it: its wind_project.toml, or why it cannot be read (moved,
// deleted, broken).
struct ProjectEntry {
    std::filesystem::path directory;
    std::optional<engine::WindProject> project;
    std::string problem;
};

[[nodiscard]] ProjectEntry read_project_entry(const std::filesystem::path& directory);

}
