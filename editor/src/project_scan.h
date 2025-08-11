#pragma once

#include "project_entry.h"

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

namespace editor {

// What scan_project read.
struct ProjectScan {
    // The project directory's own entries, sorted like ProjectEntry::children.
    std::vector<ProjectEntry> roots;
    std::size_t count = 0;
    // The scan stopped at kMaxProjectEntries.
    bool truncated = false;
};

inline constexpr std::size_t kMaxProjectEntries = 20000;

// Hidden from the Project tab: names starting with '.', the build trees (`build`, `build-*`, `cmake-build-*`,
// `out`), and `.meta` sidecars, which move with their asset.
[[nodiscard]] bool hidden_in_project(std::string_view name, bool directory);

// Walks `directory` depth first. Does not follow directory symlinks, and skips what it cannot read.
[[nodiscard]] ProjectScan scan_project(const std::filesystem::path& directory);

}
