#include "asset_watch.h"

#include "asset_sync.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <errno.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <CoreServices/CoreServices.h>
#else
#error "AssetWatch needs ReadDirectoryChangesW, inotify, or FSEvents"
#endif

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

// Blocks until `stop` or a change under `assets`. A change is `dirty = true` and nothing else.
class DirectoryWatch {
public:
    DirectoryWatch() = default;
    ~DirectoryWatch() { stop(); }

    DirectoryWatch(const DirectoryWatch&) = delete;
    DirectoryWatch& operator=(const DirectoryWatch&) = delete;

    void start(std::filesystem::path assets, std::atomic<bool>* dirty) {
        stop();
        stop_.store(false);
        dirty_ = dirty;
        if (!arm() || dirty_ == nullptr) {
            disarm();
            dirty_ = nullptr;
            return;
        }
        thread_ = std::thread([this, assets = std::move(assets)] { run(assets); });
    }

    void stop() {
        if (thread_.joinable()) {
            stop_.store(true);
            wake();
            thread_.join();
        }
        disarm();
        dirty_ = nullptr;
    }

private:
    [[nodiscard]] bool arm();
    void disarm();
    void wake();
    void run(std::filesystem::path assets);

    std::atomic<bool> stop_{false};
    std::atomic<bool>* dirty_ = nullptr;
    std::thread thread_;
#if defined(_WIN32)
    HANDLE wake_ = nullptr;
#elif defined(__linux__)
    int wake_ = -1;
#endif
};

#if defined(_WIN32)

bool DirectoryWatch::arm() {
    wake_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    return wake_ != nullptr;
}

void DirectoryWatch::disarm() {
    if (wake_ != nullptr) {
        CloseHandle(wake_);
        wake_ = nullptr;
    }
}

void DirectoryWatch::wake() {
    if (wake_ != nullptr) {
        SetEvent(wake_);
    }
}

void DirectoryWatch::run(std::filesystem::path assets) {
    constexpr DWORD kNotify = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE |
                              FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;
    while (!stop_.load()) {
        std::error_code exists_error;
        if (!std::filesystem::is_directory(assets, exists_error)) {
            if (WaitForSingleObject(wake_, 250) == WAIT_OBJECT_0) {
                break;
            }
            continue;
        }

        const HANDLE dir = CreateFileW(assets.wstring().c_str(), FILE_LIST_DIRECTORY,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (dir == INVALID_HANDLE_VALUE) {
            if (WaitForSingleObject(wake_, 250) == WAIT_OBJECT_0) {
                break;
            }
            continue;
        }

        alignas(DWORD) unsigned char buffer[64 * 1024];
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (overlapped.hEvent == nullptr) {
            CloseHandle(dir);
            break;
        }

        HANDLE waits[2] = {wake_, overlapped.hEvent};
        const auto arm_read = [&] {
            ResetEvent(overlapped.hEvent);
            return ReadDirectoryChangesW(dir, buffer, sizeof buffer, TRUE, kNotify, nullptr, &overlapped, nullptr) !=
                   FALSE;
        };

        bool pending = arm_read();
        while (pending && !stop_.load()) {
            const DWORD waited = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
            if (waited != WAIT_OBJECT_0 + 1) {
                break;
            }
            DWORD bytes = 0;
            const BOOL completed = GetOverlappedResult(dir, &overlapped, &bytes, FALSE);
            if (!completed && GetLastError() != ERROR_NOTIFY_ENUM_DIR) {
                pending = false;
                break;
            }
            pending = false;
            dirty_->store(true);
            pending = arm_read();
        }
        if (pending) {
            CancelIoEx(dir, &overlapped);
            DWORD unused = 0;
            GetOverlappedResult(dir, &overlapped, &unused, TRUE);
        }
        CloseHandle(overlapped.hEvent);
        CloseHandle(dir);
        if (stop_.load()) {
            break;
        }
        if (WaitForSingleObject(wake_, 250) == WAIT_OBJECT_0) {
            break;
        }
    }
}

#elif defined(__linux__)

bool DirectoryWatch::arm() {
    wake_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    return wake_ >= 0;
}

void DirectoryWatch::disarm() {
    if (wake_ >= 0) {
        ::close(wake_);
        wake_ = -1;
    }
}

void DirectoryWatch::wake() {
    if (wake_ >= 0) {
        const std::uint64_t one = 1;
        const ssize_t wrote = ::write(wake_, &one, sizeof one);
        (void) wrote;
    }
}

void DirectoryWatch::run(std::filesystem::path assets) {
    constexpr std::uint32_t kMask =
            IN_CREATE | IN_DELETE | IN_MODIFY | IN_ATTRIB | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF;
    const int inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd < 0) {
        return;
    }

    std::unordered_map<int, std::filesystem::path> watched;
    const auto add_one = [&](const std::filesystem::path& dir) {
        const int wd = inotify_add_watch(inotify_fd, dir.c_str(), kMask);
        if (wd >= 0) {
            watched.insert_or_assign(wd, dir);
        }
    };
    const auto add_tree = [&](const std::filesystem::path& root) {
        add_one(root);
        std::error_code iter_error;
        const auto options = std::filesystem::directory_options::skip_permission_denied;
        for (std::filesystem::recursive_directory_iterator it(root, options, iter_error);
             !iter_error && it != std::filesystem::recursive_directory_iterator(); it.increment(iter_error)) {
            std::error_code status_error;
            const std::filesystem::file_status status = it->symlink_status(status_error);
            if (!status_error && std::filesystem::is_directory(status)) {
                add_one(it->path());
            }
        }
    };
    const auto drop = [&] {
        for (const auto& [wd, dir] : watched) {
            inotify_rm_watch(inotify_fd, wd);
            (void) dir;
        }
        watched.clear();
    };

    while (!stop_.load()) {
        std::error_code exists_error;
        if (!std::filesystem::is_directory(assets, exists_error)) {
            drop();
            pollfd wake_only{.fd = wake_, .events = POLLIN, .revents = 0};
            if (::poll(&wake_only, 1, 250) > 0) {
                break;
            }
            continue;
        }
        if (watched.empty()) {
            add_tree(assets);
        }

        pollfd fds[2] = {
                {.fd = wake_, .events = POLLIN, .revents = 0},
                {.fd = inotify_fd, .events = POLLIN, .revents = 0},
        };
        const int polled = ::poll(fds, 2, -1);
        if (polled < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if ((fds[0].revents & POLLIN) != 0) {
            break;
        }
        if ((fds[1].revents & POLLIN) == 0) {
            continue;
        }

        alignas(inotify_event) char buffer[16 * 1024];
        bool overflow = false;
        while (!overflow) {
            const ssize_t read = ::read(inotify_fd, buffer, sizeof buffer);
            if (read < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            if (read == 0) {
                break;
            }
            for (ssize_t offset = 0; offset < read;) {
                const auto* event = reinterpret_cast<const inotify_event*>(buffer + offset);
                offset += static_cast<ssize_t>(sizeof(inotify_event) + event->len);
                if ((event->mask & IN_Q_OVERFLOW) != 0) {
                    overflow = true;
                    continue;
                }
                if ((event->mask & IN_IGNORED) != 0) {
                    watched.erase(event->wd);
                    continue;
                }
                dirty_->store(true);
                const bool created = (event->mask & (IN_CREATE | IN_MOVED_TO)) != 0;
                if (created && (event->mask & IN_ISDIR) != 0 && event->len > 0 && event->name[0] != '\0') {
                    const auto parent = watched.find(event->wd);
                    if (parent != watched.end()) {
                        add_tree(parent->second / event->name);
                    }
                }
            }
        }
        if (overflow) {
            dirty_->store(true);
            drop();
            add_tree(assets);
        }
    }

    drop();
    ::close(inotify_fd);
}

#elif defined(__APPLE__)

bool DirectoryWatch::arm() { return true; }

void DirectoryWatch::disarm() {}

void DirectoryWatch::wake() {}

void fs_callback(ConstFSEventStreamRef /*stream*/, void* info, size_t /*count*/, void* /*paths*/,
                 const FSEventStreamEventFlags* /*flags*/, const FSEventStreamEventId* /*ids*/) {
    static_cast<std::atomic<bool>*>(info)->store(true);
}

void DirectoryWatch::run(std::filesystem::path assets) {
    const std::u8string u8 = assets.u8string();
    const std::string bytes(reinterpret_cast<const char*>(u8.data()), u8.size());
    while (!stop_.load()) {
        std::error_code exists_error;
        if (!std::filesystem::is_directory(assets, exists_error)) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
            continue;
        }

        CFStringRef path = CFStringCreateWithFileSystemRepresentation(kCFAllocatorDefault, bytes.c_str());
        if (path == nullptr) {
            break;
        }
        CFArrayRef paths =
                CFArrayCreate(kCFAllocatorDefault, reinterpret_cast<const void**>(&path), 1, &kCFTypeArrayCallBacks);
        FSEventStreamContext context{};
        context.info = dirty_;
        FSEventStreamRef stream =
                FSEventStreamCreate(kCFAllocatorDefault, &fs_callback, &context, paths, kFSEventStreamEventIdSinceNow,
                                    0.05, kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);
        CFRelease(paths);
        CFRelease(path);
        if (stream == nullptr) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
            continue;
        }

        FSEventStreamScheduleWithRunLoop(stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        FSEventStreamStart(stream);
        while (!stop_.load() && std::filesystem::is_directory(assets, exists_error)) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
        }
        FSEventStreamStop(stream);
        FSEventStreamInvalidate(stream);
        FSEventStreamRelease(stream);
    }
}

#endif

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
