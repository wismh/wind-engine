#include "project_entry.h"

#include <utility>

namespace launcher {

ProjectEntry read_project_entry(const std::filesystem::path& directory) {
    ProjectEntry entry{.directory = directory, .project = std::nullopt, .problem = {}};
    if (auto project = engine::read_wind_project(directory)) {
        entry.project = std::move(*project);
    } else {
        entry.problem = engine::describe(project.error());
    }
    return entry;
}

}
