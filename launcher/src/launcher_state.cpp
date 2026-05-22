#include "launcher_state.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace launcher {
namespace {

constexpr std::string_view kProjectKey = "project=";
constexpr std::string_view kSdkKey = "sdk=";

std::filesystem::path path_from(std::string_view text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string comparable(const std::filesystem::path& path) {
    std::filesystem::path normal = std::filesystem::absolute(path).lexically_normal();
    if (!normal.has_filename() && normal.has_parent_path() && normal != normal.root_path()) {
        normal = normal.parent_path();
    }
    std::string text = path_text(normal);
#if defined(_WIN32)
    std::ranges::transform(text, text.begin(),
            [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
#endif
    return text;
}

void forget(std::vector<std::filesystem::path>& paths, const std::filesystem::path& path) {
    std::erase_if(paths, [&](const std::filesystem::path& entry) { return same_directory(entry, path); });
}

}

std::string path_text(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

LauncherState parse_launcher_state(std::string_view text) {
    LauncherState state;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.starts_with(kProjectKey) && line.size() > kProjectKey.size()) {
            state.projects.push_back(path_from(line.substr(kProjectKey.size())));
        } else if (line.starts_with(kSdkKey) && line.size() > kSdkKey.size()) {
            state.sdks.push_back(path_from(line.substr(kSdkKey.size())));
        }
    }
    return state;
}

std::string format_launcher_state(const LauncherState& state) {
    std::string text = "# Wind Launcher. Written by the launcher; one entry per line.\n";
    for (const std::filesystem::path& project : state.projects) {
        text += std::string(kProjectKey) + path_text(project) + "\n";
    }
    for (const std::filesystem::path& sdk : state.sdks) {
        text += std::string(kSdkKey) + path_text(sdk) + "\n";
    }
    return text;
}

LauncherState load_launcher_state(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return {};
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parse_launcher_state(text);
}

bool save_launcher_state(const std::filesystem::path& file, const LauncherState& state) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << format_launcher_state(state);
    return static_cast<bool>(out);
}

void remember_project(LauncherState& state, const std::filesystem::path& directory) {
    forget(state.projects, directory);
    state.projects.insert(state.projects.begin(), directory);
}

void forget_project(LauncherState& state, const std::filesystem::path& directory) {
    forget(state.projects, directory);
}

void remember_sdk(LauncherState& state, const std::filesystem::path& root) {
    const bool known = std::ranges::any_of(
            state.sdks, [&](const std::filesystem::path& entry) { return same_directory(entry, root); });
    if (!known) {
        state.sdks.push_back(root);
    }
}

void forget_sdk(LauncherState& state, const std::filesystem::path& root) {
    forget(state.sdks, root);
}

bool same_directory(const std::filesystem::path& a, const std::filesystem::path& b) {
    return comparable(a) == comparable(b);
}

}
