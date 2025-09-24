#include "project_check.h"

#include <system_error>
#include <utility>

namespace editor {

ProjectCheck check_project(const std::filesystem::path& directory, const std::optional<engine::SdkManifest>& sdk) {
    ProjectCheck check;
    std::error_code error;
    const std::filesystem::path canonical =
            std::filesystem::weakly_canonical(std::filesystem::absolute(directory), error);
    check.directory = error ? std::filesystem::absolute(directory) : canonical;
    auto project = engine::read_wind_project(check.directory);
    if (!project) {
        check.problem = engine::describe(project.error());
        return check;
    }
    check.project = std::move(*project);
    if (!sdk) {
        check.problem = "This editor is not an installed SDK (no sdk.toml beside bin/): install it with "
                        "cmake --install to build projects.";
        return check;
    }
    if (check.project->engine != sdk->version) {
        check.problem = "The project needs engine " + check.project->engine + "; this editor is " + sdk->version +
                ". Open it with that version.";
        return check;
    }
    check.fits = true;
    return check;
}

}
