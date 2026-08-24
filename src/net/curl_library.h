#pragma once

#include "net/curl_api.h"

#if defined(ENGINE_NET_CURL)

namespace engine {

// The system libcurl, opened with dlopen: libcurl.so.4 on Linux, libcurl.4.dylib on macOS (always installed).
// Nothing is linked, so a build needs no libcurl headers and a machine without libcurl runs the game with
// `HttpError::Unsupported` instead of failing to start.
class CurlLibrary {
public:
    CurlLibrary() = default;
    CurlLibrary(const CurlLibrary&) = delete;
    CurlLibrary& operator=(const CurlLibrary&) = delete;
    ~CurlLibrary();

    // False if no libcurl was found or one of its functions is missing. Calls curl_global_init.
    [[nodiscard]] bool load();
    void unload();

    [[nodiscard]] bool loaded() const {
        return library_ != nullptr;
    }
    [[nodiscard]] const CurlApi& api() const {
        return api_;
    }

private:
    void* library_ = nullptr;
    CurlApi api_{};
    using GlobalCleanup = void (*)();
    GlobalCleanup global_cleanup_ = nullptr;
};

}

#endif
