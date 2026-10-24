#include "net/curl_library.h"

#if defined(ENGINE_NET_CURL)

#include <dlfcn.h>

namespace engine {
namespace {

#if defined(__APPLE__)
constexpr const char* kNames[] = {"libcurl.4.dylib", "/usr/lib/libcurl.4.dylib", "libcurl.dylib"};
#else
constexpr const char* kNames[] = {"libcurl.so.4", "libcurl.so"};
#endif

template <typename Function>
bool symbol(void* library, const char* name, Function& out) {
    out = reinterpret_cast<Function>(dlsym(library, name));
    return out != nullptr;
}

}

CurlLibrary::~CurlLibrary() {
    unload();
}

bool CurlLibrary::load() {
    unload();
    for (const char* name : kNames) {
        library_ = dlopen(name, RTLD_NOW | RTLD_LOCAL);
        if (library_ != nullptr) {
            break;
        }
    }
    if (library_ == nullptr) {
        return false;
    }

    using GlobalInit = int (*)(long);
    GlobalInit global_init = nullptr;
    const bool ok = symbol(library_, "curl_global_init", global_init) &&
                    symbol(library_, "curl_global_cleanup", global_cleanup_) &&
                    symbol(library_, "curl_easy_init", api_.easy_init) &&
                    symbol(library_, "curl_easy_cleanup", api_.easy_cleanup) &&
                    symbol(library_, "curl_easy_setopt", api_.easy_setopt) &&
                    symbol(library_, "curl_easy_perform", api_.easy_perform) &&
                    symbol(library_, "curl_slist_append", api_.slist_append) &&
                    symbol(library_, "curl_slist_free_all", api_.slist_free_all);
    if (!ok || global_init(curl::kGlobalDefault) != curl::kOk) {
        global_cleanup_ = nullptr;
        unload();
        return false;
    }
    return true;
}

void CurlLibrary::unload() {
    if (library_ == nullptr) {
        return;
    }
    if (global_cleanup_ != nullptr) {
        global_cleanup_();
    }
    dlclose(library_);
    library_ = nullptr;
    api_ = {};
    global_cleanup_ = nullptr;
}

}

#endif
