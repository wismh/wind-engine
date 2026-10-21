#if defined(__APPLE__)

#include "directory_watch.h"

#include <CoreServices/CoreServices.h>

#include <string>
#include <system_error>

namespace editor {

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

}

#endif
