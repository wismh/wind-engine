#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <thread>

namespace editor {

// Blocks until `stop` or a change under `assets`. A change is `dirty = true` and nothing else. The OS part is one
// file per platform (`directory_watch_win32.cpp`, `_linux.cpp`, `_macos.cpp`): ReadDirectoryChangesW, inotify,
// FSEvents. The others compile to nothing.
class DirectoryWatch {
public:
    DirectoryWatch() = default;
    ~DirectoryWatch();

    DirectoryWatch(const DirectoryWatch&) = delete;
    DirectoryWatch& operator=(const DirectoryWatch&) = delete;

    void start(std::filesystem::path assets, std::atomic<bool>* dirty);
    void stop();

private:
    [[nodiscard]] bool arm();
    void disarm();
    void wake();
    void run(std::filesystem::path assets);

    std::atomic<bool> stop_{false};
    std::atomic<bool>* dirty_ = nullptr;
    std::thread thread_;
    // The event handle (Windows) or eventfd (Linux) that ends a wait. Unused on macOS.
    static constexpr std::intptr_t kNoWake = -1;
    std::intptr_t wake_ = kNoWake;
};

}
