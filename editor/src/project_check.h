#pragma once

#include <engine/project/sdk_manifest.h>
#include <engine/project/wind_project.h>

#include <filesystem>
#include <optional>
#include <string>

namespace editor {

// A project directory read and held against this editor's SDK.
struct ProjectCheck {
    // The directory made absolute and canonical (as far as it exists).
    std::filesystem::path directory;
    // wind_project.toml, when it was read.
    std::optional<engine::WindProject> project;
    // The project was read and its engine version is the SDK's: it can be built.
    bool fits = false;
    // Why the project cannot be built, when it cannot. Empty when `fits`. When `project` is empty this is the
    // reading failure.
    std::string problem;
};

// Reads `<directory>/wind_project.toml` and checks it against `sdk`: no manifest means this editor is not an
// installed SDK, and a project that needs another engine version is not built by this one.
[[nodiscard]] ProjectCheck check_project(const std::filesystem::path& directory,
        const std::optional<engine::SdkManifest>& sdk);

}
