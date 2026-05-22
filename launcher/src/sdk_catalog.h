#pragma once

#include <engine/process/process_desc.h>
#include <engine/project/sdk_manifest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace launcher {

// One installed editor SDK the launcher can start.
struct SdkEntry {
    std::filesystem::path root;
    engine::SdkManifest manifest;
    // Remembered by Locate (and forgettable), not found under the install directory.
    bool located = false;
};

// Every SDK of `install_dir/*/sdk.toml`, then every `located` root, without duplicates. Newest version first; for one
// version a clean SDK before a dirty one. A located root without a readable sdk.toml is skipped and described in
// `problems`.
[[nodiscard]] std::vector<SdkEntry> find_sdks(const std::filesystem::path& install_dir,
        const std::vector<std::filesystem::path>& located, std::vector<std::string>& problems);

// The SDK a project of engine `version` opens with: the first of `sdks` (already in preference order) with exactly that
// version, or nullptr.
[[nodiscard]] const SdkEntry* sdk_for(const std::vector<SdkEntry>& sdks, std::string_view version);

// Semver order of two versions, numeric per dot-separated part; a part that is not a number compares as text.
// Negative, zero, or positive like strcmp.
[[nodiscard]] int compare_versions(std::string_view a, std::string_view b);

// `<sdk>/bin/wind_editor(.exe) --project <project>`, started in `<sdk>/bin`.
[[nodiscard]] std::filesystem::path editor_executable(const SdkEntry& sdk);
[[nodiscard]] engine::ProcessDesc editor_launch(const SdkEntry& sdk, const std::filesystem::path& project);

}
