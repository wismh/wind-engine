#include "user_paths.h"

#include <cstdlib>
#include <string>

namespace launcher {

std::filesystem::path environment_path(const char* name) {
#if defined(_WIN32)
    const std::string narrow(name);
    const std::wstring wide(narrow.begin(), narrow.end());
    wchar_t* value = nullptr;
    std::size_t size = 0;
    if (_wdupenv_s(&value, &size, wide.c_str()) != 0 || value == nullptr) {
        return {};
    }
    std::filesystem::path path(value);
    std::free(value);
    return path;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::filesystem::path{} : std::filesystem::path(value);
#endif
}

std::filesystem::path default_project_location() {
#if defined(_WIN32)
    const std::filesystem::path home = environment_path("USERPROFILE");
#else
    const std::filesystem::path home = environment_path("HOME");
#endif
    return home.empty() ? home : home / "WindProjects";
}

}
