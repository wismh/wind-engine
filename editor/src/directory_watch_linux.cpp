#if defined(__linux__)

#include "directory_watch.h"

#include <cerrno>
#include <cstdint>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <system_error>
#include <unordered_map>

namespace editor {

bool DirectoryWatch::arm() {
    const int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    wake_ = fd >= 0 ? fd : kNoWake;
    return fd >= 0;
}

void DirectoryWatch::disarm() {
    if (wake_ != kNoWake) {
        ::close(static_cast<int>(wake_));
        wake_ = kNoWake;
    }
}

void DirectoryWatch::wake() {
    if (wake_ != kNoWake) {
        const std::uint64_t one = 1;
        const ssize_t wrote = ::write(static_cast<int>(wake_), &one, sizeof one);
        (void) wrote;
    }
}

void DirectoryWatch::run(std::filesystem::path assets) {
    constexpr std::uint32_t kMask =
            IN_CREATE | IN_DELETE | IN_MODIFY | IN_ATTRIB | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF;
    const int wake_fd = static_cast<int>(wake_);
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
            pollfd wake_only{.fd = wake_fd, .events = POLLIN, .revents = 0};
            if (::poll(&wake_only, 1, 250) > 0) {
                break;
            }
            continue;
        }
        if (watched.empty()) {
            add_tree(assets);
        }

        pollfd fds[2] = {
                {.fd = wake_fd, .events = POLLIN, .revents = 0},
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

}

#endif
