#if defined(_WIN32)

#include "directory_watch.h"

#include <system_error>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace editor {

bool DirectoryWatch::arm() {
    const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    wake_ = event != nullptr ? reinterpret_cast<std::intptr_t>(event) : kNoWake;
    return event != nullptr;
}

void DirectoryWatch::disarm() {
    if (wake_ != kNoWake) {
        CloseHandle(reinterpret_cast<HANDLE>(wake_));
        wake_ = kNoWake;
    }
}

void DirectoryWatch::wake() {
    if (wake_ != kNoWake) {
        SetEvent(reinterpret_cast<HANDLE>(wake_));
    }
}

void DirectoryWatch::run(std::filesystem::path assets) {
    const HANDLE wake_event = reinterpret_cast<HANDLE>(wake_);
    constexpr DWORD kNotify = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE |
                              FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;
    while (!stop_.load()) {
        std::error_code exists_error;
        if (!std::filesystem::is_directory(assets, exists_error)) {
            if (WaitForSingleObject(wake_event, 250) == WAIT_OBJECT_0) {
                break;
            }
            continue;
        }

        const HANDLE dir = CreateFileW(assets.wstring().c_str(), FILE_LIST_DIRECTORY,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (dir == INVALID_HANDLE_VALUE) {
            if (WaitForSingleObject(wake_event, 250) == WAIT_OBJECT_0) {
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

        HANDLE waits[2] = {wake_event, overlapped.hEvent};
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
        if (WaitForSingleObject(wake_event, 250) == WAIT_OBJECT_0) {
            break;
        }
    }
}

}

#endif
