#pragma once

// docs/tech/modules/Project.md

#include <engine/project/manifest_error.h>

#include <expected>
#include <filesystem>
#include <string>

namespace engine {

// The file at an installed editor SDK's root (written by cmake/sdk_manifest.cmake on install).
inline constexpr char kSdkManifestFile[] = "sdk.toml";

// sdk.toml. Every key is required; keys the reader does not know are ignored, so an older reader reads a newer SDK.
struct SdkManifest {
    // project(engine VERSION), semver.
    std::string version;
    // The engine commit the SDK was built from.
    std::string commit;
    // The engine checkout had changes: not the build of its version's tag.
    bool dirty = false;
    // The installed configuration (Release, Debug).
    std::string config;
    // kBuildId of that configuration.
    std::string build_id;
};

// Reads `<sdk_root>/sdk.toml`.
[[nodiscard]] std::expected<SdkManifest, ManifestFailure> read_sdk_manifest(const std::filesystem::path& sdk_root);

}
