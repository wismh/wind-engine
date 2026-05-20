#include <engine/project/sdk_manifest.h>

#include "project/manifest_table.h"

#include <utility>

namespace engine {

std::expected<SdkManifest, ManifestFailure> read_sdk_manifest(const std::filesystem::path& sdk_root) {
    const std::filesystem::path path = sdk_root / kSdkManifestFile;
    const std::expected<ManifestTable, ManifestFailure> table = ManifestTable::load(path);
    if (!table) {
        return std::unexpected(table.error());
    }
    SdkManifest manifest;
    for (const auto& [key, field] : {std::pair{"version", &manifest.version}, std::pair{"commit", &manifest.commit},
                 std::pair{"config", &manifest.config}, std::pair{"build_id", &manifest.build_id}}) {
        std::expected<std::string, ManifestFailure> value = table->string(key);
        if (!value) {
            return std::unexpected(in_manifest(std::move(value.error()), path));
        }
        *field = std::move(*value);
    }
    const std::expected<bool, ManifestFailure> dirty = table->boolean("dirty");
    if (!dirty) {
        return std::unexpected(in_manifest(dirty.error(), path));
    }
    manifest.dirty = *dirty;
    return manifest;
}

}
