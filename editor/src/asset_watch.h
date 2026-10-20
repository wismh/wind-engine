#pragma once

#include <chrono>
#include <filesystem>
#include <memory>

namespace editor {

// Watches `<project>/assets` while a project is open. A background thread blocks on the OS (ReadDirectoryChangesW,
// inotify, or FSEvents) and only raises a flag; it does not walk the tree and does not call engine APIs.
// `poll` runs on the editor thread. Idle polls do not walk. After a change, a fingerprint is taken at most once
// per `kSampleInterval`. The first fingerprint that differs from the synced tree is kept; the next one that
// matches it runs `sync_assets` and returns true so the caller can rescan. `open` syncs immediately.
class AssetWatch {
public:
    static constexpr std::chrono::milliseconds kSampleInterval{250};

    AssetWatch();
    ~AssetWatch();

    AssetWatch(const AssetWatch&) = delete;
    AssetWatch& operator=(const AssetWatch&) = delete;

    // Syncs `<project_dir>/assets` now, then watches it. Replaces any watch already open.
    void open(const std::filesystem::path& project_dir);
    void close();

    // The OS thread calls this. Tests call it too, so they do not wait for the OS.
    void notice();

    // True when a settled change was synced: the project tree should be scanned again.
    [[nodiscard]] bool poll(std::chrono::steady_clock::time_point now);

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace editor
