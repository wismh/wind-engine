#include "project_scan.h"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <utility>

namespace editor {
namespace {

std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::string folded(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool before(const ProjectEntry& a, const ProjectEntry& b) {
    if (a.directory != b.directory) {
        return a.directory;
    }
    const std::string fa = folded(a.name);
    const std::string fb = folded(b.name);
    return fa != fb ? fa < fb : a.name < b.name;
}

// `prefix` is the key of `directory`, empty for the project directory itself.
std::vector<ProjectEntry> read_directory(const std::filesystem::path& directory, const std::string& prefix,
        ProjectScan& scan) {
    std::vector<ProjectEntry> entries;
    std::error_code error;
    std::filesystem::directory_iterator it(directory, std::filesystem::directory_options::skip_permission_denied,
            error);
    for (; !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
        if (scan.count >= kMaxProjectEntries) {
            scan.truncated = true;
            break;
        }
        const std::filesystem::directory_entry& item = *it;
        std::error_code status_error;
        const std::filesystem::file_status status = item.symlink_status(status_error);
        if (status_error) {
            continue;
        }
        ProjectEntry entry;
        entry.directory = std::filesystem::is_directory(status);
        entry.name = utf8(item.path().filename());
        if (hidden_in_project(entry.name, entry.directory)) {
            continue;
        }
        entry.key = prefix.empty() ? entry.name : prefix + "/" + entry.name;
        ++scan.count;
        if (entry.directory) {
            entry.children = read_directory(item.path(), entry.key, scan);
        } else {
            std::error_code size_error;
            const std::uintmax_t size = item.file_size(size_error);
            entry.size = size_error ? 0 : size;
        }
        entries.push_back(std::move(entry));
    }
    std::ranges::sort(entries, before);
    return entries;
}

}

bool hidden_in_project(std::string_view name, bool directory) {
    if (name.empty() || name.front() == '.') {
        return true;
    }
    if (directory) {
        return name == "build" || name == "out" || name.starts_with("build-") || name.starts_with("cmake-build-");
    }
    return name.ends_with(".meta");
}

ProjectScan scan_project(const std::filesystem::path& directory) {
    ProjectScan scan;
    scan.roots = read_directory(directory, std::string(), scan);
    return scan;
}

}
