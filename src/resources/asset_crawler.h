#pragma once

#include <engine/resources/asset_id.h>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine {

// Information about an asset in the source tree used for dependency traversal.
struct ScannedAssetInfo {
    AssetId guid;
    std::filesystem::path relative_path;
    std::filesystem::path full_path;
};

// Extracts all `asset://<32-hex-guid>` references from content.
[[nodiscard]] std::vector<AssetId> extract_asset_dependencies(std::string_view content);

// Simple glob pattern matching: supports '*' (any characters) and '**'.
[[nodiscard]] bool glob_match(std::string_view pattern, std::string_view path);

// Computes the set of reachable AssetIds given a set of scanned assets and filter options.
// If roots and always_include are both empty, all scanned assets are considered reachable.
[[nodiscard]] std::unordered_set<AssetId> crawl_reachable_assets(
        const std::unordered_map<AssetId, ScannedAssetInfo>& scanned_assets,
        const std::unordered_map<std::string, AssetId>& path_to_guid,
        std::span<const std::string> roots = {},
        std::span<const std::string> always_include = {},
        std::string* error_out = nullptr);

} // namespace engine
