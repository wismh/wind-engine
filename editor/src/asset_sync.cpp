#include "asset_sync.h"

#include <engine/log.h>
#include <engine/resources/asset_guid.h>

#include <format>
#include <system_error>
#include <vector>

namespace editor {
namespace {

// `foo.png.meta` -> `foo.png`. The last extension only, so a stem that itself has a dot stays.
std::filesystem::path source_of_meta(const std::filesystem::path& meta) {
    std::filesystem::path source = meta;
    source.replace_extension();
    return source;
}

int remove_orphan_metas(const std::filesystem::path& root) {
    std::error_code exists_error;
    if (!std::filesystem::exists(root, exists_error) || !std::filesystem::is_directory(root)) {
        return 0;
    }

    // Collected first: deleting the file the iterator is on would skip the next entry.
    std::vector<std::filesystem::path> orphans;
    std::error_code iter_error;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator it(root, options, iter_error);
         !iter_error && it != std::filesystem::recursive_directory_iterator(); it.increment(iter_error)) {
        std::error_code status_error;
        if (!it->is_regular_file(status_error) || status_error) {
            continue;
        }
        const std::filesystem::path& meta = it->path();
        if (meta.extension() != ".meta") {
            continue;
        }
        std::error_code source_error;
        if (!std::filesystem::exists(source_of_meta(meta), source_error)) {
            orphans.push_back(meta);
        }
    }

    int removed = 0;
    for (const std::filesystem::path& meta : orphans) {
        std::error_code remove_error;
        if (std::filesystem::remove(meta, remove_error)) {
            ++removed;
        }
    }
    return removed;
}

} // namespace

AssetSyncCounts sync_assets(const std::filesystem::path& root) {
    AssetSyncCounts counts;
    counts.written = engine::write_missing_metas(root);
    counts.removed = remove_orphan_metas(root);
    if (counts.written != 0 || counts.removed != 0) {
        engine::log::info(
                std::format("Editor: assets sync wrote {} .meta, removed {} .meta", counts.written, counts.removed));
    }
    return counts;
}

} // namespace editor
