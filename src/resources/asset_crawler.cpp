#include "resources/asset_crawler.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace engine {

std::vector<AssetId> extract_asset_dependencies(std::string_view content) {
    std::vector<AssetId> dependencies;
    std::size_t pos = 0;
    while ((pos = content.find(AssetId::kUriPrefix, pos)) != std::string_view::npos) {
        if (pos + AssetId::kUriLength <= content.size()) {
            const std::string_view candidate = content.substr(pos, AssetId::kUriLength);
            if (const auto id = AssetId::parse_uri(candidate)) {
                dependencies.push_back(*id);
                pos += AssetId::kUriLength;
                continue;
            }
        }
        pos += AssetId::kUriPrefix.size();
    }
    return dependencies;
}

bool glob_match(std::string_view pattern, std::string_view path) {
    std::size_t p = 0;
    std::size_t s = 0;
    std::size_t star_p = std::string_view::npos;
    std::size_t star_s = 0;

    while (s < path.size()) {
        if (p < pattern.size() && pattern[p] == '*') {
            if (p + 1 < pattern.size() && pattern[p + 1] == '*') {
                p += 2;
                if (p == pattern.size()) {
                    return true;
                }
                star_p = p;
                star_s = s;
                continue;
            }
            star_p = ++p;
            star_s = s;
        } else if (p < pattern.size() && (pattern[p] == path[s] || pattern[p] == '?')) {
            ++p;
            ++s;
        } else if (star_p != std::string_view::npos) {
            p = star_p;
            s = ++star_s;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

std::unordered_set<AssetId> crawl_reachable_assets(
        const std::unordered_map<AssetId, ScannedAssetInfo>& scanned_assets,
        const std::unordered_map<std::string, AssetId>& path_to_guid,
        std::span<const std::string> roots,
        std::span<const std::string> always_include,
        std::string* error_out) {
    if (roots.empty() && always_include.empty()) {
        std::unordered_set<AssetId> all;
        all.reserve(scanned_assets.size());
        for (const auto& [id, info] : scanned_assets) {
            (void)info;
            all.insert(id);
        }
        return all;
    }

    std::unordered_set<AssetId> reachable;
    std::vector<AssetId> queue;

    auto add_reachable = [&](AssetId id) {
        if (reachable.insert(id).second) {
            queue.push_back(id);
        }
    };

    for (std::string_view root : roots) {
        std::string normalized(root);
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        const auto it = path_to_guid.find(normalized);
        if (it == path_to_guid.end()) {
            if (error_out != nullptr) {
                *error_out = "asset root not found: " + std::string(root);
            }
            return {};
        }
        add_reachable(it->second);
    }

    for (std::string_view pattern : always_include) {
        std::string norm_pattern(pattern);
        std::replace(norm_pattern.begin(), norm_pattern.end(), '\\', '/');
        for (const auto& [id, info] : scanned_assets) {
            if (glob_match(norm_pattern, info.relative_path.generic_string())) {
                add_reachable(id);
            }
        }
    }

    while (!queue.empty()) {
        const AssetId current = queue.back();
        queue.pop_back();

        const auto it = scanned_assets.find(current);
        if (it == scanned_assets.end()) {
            continue;
        }

        std::ifstream file_in(it->second.full_path, std::ios::binary);
        if (!file_in) {
            continue;
        }
        const std::string content((std::istreambuf_iterator<char>(file_in)), std::istreambuf_iterator<char>());
        const auto deps = extract_asset_dependencies(content);
        for (const AssetId dep : deps) {
            if (scanned_assets.contains(dep)) {
                add_reachable(dep);
            }
        }

        if (it->second.relative_path.extension() == ".xml") {
            std::size_t pos = 0;
            while ((pos = content.find("src=\"", pos)) != std::string::npos) {
                pos += 5;
                const std::size_t end = content.find('"', pos);
                if (end != std::string::npos) {
                    const std::string src(content.substr(pos, end - pos));
                    if (!src.starts_with(AssetId::kUriPrefix)) {
                        const auto full_child = (it->second.full_path.parent_path() / src).lexically_normal();
                        for (const auto& [id, info] : scanned_assets) {
                            if (info.full_path == full_child) {
                                add_reachable(id);
                                break;
                            }
                        }
                    }
                    pos = end + 1;
                }
            }
        }
    }

    return reachable;
}

} // namespace engine
