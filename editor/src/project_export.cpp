#include "project_export.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace editor {
namespace {

std::string path_text(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::filesystem::path normal(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(std::filesystem::absolute(path), error);
    return (error ? std::filesystem::absolute(path) : canonical).lexically_normal();
}

// True when `inner` is `outer` or lies under it. Both normal.
bool is_within(const std::filesystem::path& inner, const std::filesystem::path& outer) {
    auto outer_part = outer.begin();
    auto inner_part = inner.begin();
    for (; outer_part != outer.end(); ++outer_part, ++inner_part) {
        if (*outer_part == "") {
            // A trailing separator.
            return true;
        }
        if (inner_part == inner.end() || *inner_part != *outer_part) {
            return false;
        }
    }
    return true;
}

std::string message(const std::string& what, const std::filesystem::path& path, const std::error_code& error) {
    return what + " " + path_text(path) + ": " + error.message();
}

// Why an Export may not empty `target`, or empty when it may: it does not exist, is empty, or an earlier Export made
// it (kExportMarker). Anything else is somebody's files.
std::string not_ours_problem(const std::filesystem::path& target) {
    std::error_code error;
    if (!std::filesystem::exists(target, error)) {
        return {};
    }
    if (!std::filesystem::is_directory(target, error)) {
        return "The export directory " + path_text(target) + " is a file.";
    }
    if (std::filesystem::exists(target / kExportMarker, error) || std::filesystem::is_empty(target, error)) {
        return {};
    }
    return "The export directory " + path_text(target) + " is not empty and was not made by an Export (it has no " +
            kExportMarker + "). Choose an empty or new directory; Export empties its target.";
}

}

std::filesystem::path default_export_directory(const std::filesystem::path& project, const std::string& target) {
    return project / "export" / target;
}

std::string export_directory_problem(const std::filesystem::path& project, const std::filesystem::path& directory) {
    if (directory.empty()) {
        return "The export directory is empty.";
    }
    const std::filesystem::path target = normal(directory);
    if (target == target.root_path()) {
        return "The export directory " + path_text(target) + " is a file system root.";
    }
    if (is_within(normal(project), target)) {
        return "The export directory " + path_text(target) + " holds the project; exporting would empty it.";
    }
    return not_ours_problem(target);
}

bool skipped_in_export(const std::filesystem::path& entry) {
    constexpr std::array<std::string_view, 9> kSkippedExtensions{
            ".pdb", ".ilk", ".exp", ".lib", ".idb", ".obj", ".o", ".a", ".tlog"};
    const std::string name = entry.filename().string();
    if (name == "CMakeFiles") {
        return true;
    }
    const std::string extension = entry.extension().string();
    return std::ranges::any_of(kSkippedExtensions, [&](std::string_view skipped) {
        return std::ranges::equal(extension, skipped, [](char a, char b) {
            return (a >= 'A' && a <= 'Z' ? static_cast<char>(a - 'A' + 'a') : a) == b;
        });
    });
}

std::expected<std::filesystem::path, std::string> copy_export(
        const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code error;
    if (!std::filesystem::is_directory(from, error)) {
        return std::unexpected("The build output " + path_text(from) + " is not a directory.");
    }
    const std::filesystem::path source = normal(from);
    const std::filesystem::path target = normal(to);
    if (to.empty() || target == target.root_path()) {
        return std::unexpected("The export directory " + path_text(to) + " is not usable.");
    }
    if (is_within(source, target) || is_within(target, source)) {
        return std::unexpected("The export directory " + path_text(target) + " and the build output " +
                path_text(source) + " overlap.");
    }
    if (std::string problem = not_ours_problem(target); !problem.empty()) {
        return std::unexpected(std::move(problem));
    }
    std::filesystem::remove_all(target, error);
    if (error) {
        return std::unexpected(message("Could not empty", target, error));
    }
    std::filesystem::create_directories(target, error);
    if (error) {
        return std::unexpected(message("Could not create", target, error));
    }
    // Before the copy, so a failed copy still leaves a directory the next Export may empty.
    std::ofstream(target / kExportMarker, std::ios::binary)
            << "Made by a Wind Export. The next Export to this directory empties it first.\n";
    for (std::filesystem::recursive_directory_iterator it(source, error), end; it != end; it.increment(error)) {
        if (error) {
            break;
        }
        const std::filesystem::path& entry = it->path();
        if (skipped_in_export(entry)) {
            if (it->is_directory(error)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        const std::filesystem::path destination = target / entry.lexically_relative(source);
        if (it->is_directory(error)) {
            std::filesystem::create_directories(destination, error);
        } else if (it->is_regular_file(error)) {
            std::filesystem::create_directories(destination.parent_path(), error);
            if (!error) {
                std::filesystem::copy_file(entry, destination, std::filesystem::copy_options::overwrite_existing, error);
            }
        }
        if (error) {
            return std::unexpected(message("Could not copy", entry, error));
        }
    }
    if (error) {
        return std::unexpected(message("Could not read", source, error));
    }
    return target;
}

}
