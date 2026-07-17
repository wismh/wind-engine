#include "asset_watch.h"

#include "asset_sync.h"
#include "directory_watch.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace editor {
namespace {

struct FingerprintEntry {
    std::string path;
    bool directory = false;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type modified{};

    bool operator==(const FingerprintEntry&) const = default;
};

struct Fingerprint {
    // False when `assets/` does not exist. An empty directory is present with no entries.
    bool present = false;
    std::vector<FingerprintEntry> entries;

    bool operator==(const Fingerprint&) const = default;
};

std::string utf8_relative(const std::filesystem::path& root, const std::filesystem::path& path) {
    const std::u8string text = path.lexically_relative(root).generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

// Relative path of every directory, plus size and mtime of every file. Directory symlinks are not followed.
Fingerprint fingerprint(const std::filesystem::path& root) {
    Fingerprint out;
    std::error_code exists_error;
    if (!std::filesystem::is_directory(root, exists_error)) {
        return out;
    }
    out.present = true;

    std::error_code iter_error;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator it(root, options, iter_error);
         !iter_error && it != std::filesystem::recursive_directory_iterator(); it.increment(iter_error)) {
        std::error_code status_error;
        const std::filesystem::file_status status = it->symlink_status(status_error);
        if (status_error) {
            continue;
        }
        FingerprintEntry entry;
        entry.path = utf8_relative(root, it->path());
        entry.directory = std::filesystem::is_directory(status);
        if (!entry.directory) {
            std::error_code size_error;
            const std::uintmax_t size = it->file_size(size_error);
            entry.size = size_error ? 0 : size;
            std::error_code time_error;
            entry.modified = it->last_write_time(time_error);
        }
        out.entries.push_back(std::move(entry));
    }
    std::ranges::sort(out.entries,
                      [](const FingerprintEntry& a, const FingerprintEntry& b) { return a.path < b.path; });
    return out;
}

} // namespace

struct AssetWatch::State {
    std::filesystem::path assets;
    Fingerprint baseline;
    std::optional<Fingerprint> pending;
    std::optional<std::chrono::steady_clock::time_point> sampled;
    bool open = false;
    std::atomic<bool> dirty{false};
    DirectoryWatch watch;
};

AssetWatch::AssetWatch() : state_(std::make_unique<State>()) {}

AssetWatch::~AssetWatch() { close(); }

void AssetWatch::open(const std::filesystem::path& project_dir) {
    close();
    state_->assets = project_dir / "assets";
    (void) sync_assets(state_->assets);
    state_->baseline = fingerprint(state_->assets);
    state_->pending.reset();
    state_->sampled.reset();
    state_->dirty.store(false);
    state_->open = true;
    state_->watch.start(state_->assets, &state_->dirty);
}

void AssetWatch::close() {
    state_->watch.stop();
    state_->open = false;
    state_->pending.reset();
    state_->sampled.reset();
    state_->dirty.store(false);
    state_->assets.clear();
}

void AssetWatch::notice() { state_->dirty.store(true); }

bool AssetWatch::poll(std::chrono::steady_clock::time_point now) {
    State& state = *state_;
    if (!state.open) {
        return false;
    }
    const bool dirty = state.dirty.exchange(false);
    if (!dirty && !state.pending) {
        return false;
    }
    if (state.sampled && now < *state.sampled + kSampleInterval) {
        if (dirty) {
            state.dirty.store(true);
        }
        return false;
    }

    state.sampled = now;
    Fingerprint current = fingerprint(state.assets);
    if (current == state.baseline) {
        state.pending.reset();
        return false;
    }
    if (state.pending && *state.pending == current) {
        (void) sync_assets(state.assets);
        state.baseline = fingerprint(state.assets);
        state.pending.reset();
        return true;
    }
    state.pending = std::move(current);
    return false;
}

} // namespace editor
