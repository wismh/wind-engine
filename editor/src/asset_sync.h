#pragma once

#include <filesystem>

namespace editor {

// How many sidecars `sync_assets` wrote and how many orphan `.meta` files it deleted.
struct AssetSyncCounts {
    int written = 0;
    int removed = 0;
};

// Makes `<root>` match the asset rules: `write_missing_metas` for files that have no sidecar, then deletes a
// `.meta` whose source path is gone (neither a file nor a directory). Never overwrites an existing sidecar.
// A missing `root` does nothing. One log line when either count is non-zero.
[[nodiscard]] AssetSyncCounts sync_assets(const std::filesystem::path& root);

} // namespace editor
