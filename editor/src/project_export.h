#pragma once

#include <expected>
#include <filesystem>
#include <string>

namespace editor {

// The file an Export puts in its target. Export empties its target, so it only does that to a directory that does not
// exist, is empty, or has this file; any other directory is somebody's files and is refused.
inline constexpr char kExportMarker[] = ".wind_export";

// <project>/export/<target>: where an Export puts the game when nobody names a directory.
[[nodiscard]] std::filesystem::path default_export_directory(
        const std::filesystem::path& project, const std::string& target);

// Why `directory` cannot be the target of an Export of `project`, or empty when it can. Export empties its target
// first, so the project itself, a directory that holds it, a file system root, a file, and a non-empty directory
// without kExportMarker are refused.
[[nodiscard]] std::string export_directory_problem(
        const std::filesystem::path& project, const std::filesystem::path& directory);

// True for a file or directory of a build that a shipped game does not need: debug symbols and linker output
// (*.pdb, *.ilk, *.exp, *.lib, *.idb), object files (*.obj, *.o), static archives (*.a), and CMakeFiles.
[[nodiscard]] bool skipped_in_export(const std::filesystem::path& entry);

// Copies the build output `from` (the executable and its assets/) to `to`: `to` is emptied or created first (same
// rule as export_directory_problem: nothing is deleted from a directory that is not empty and has no kExportMarker),
// kExportMarker is written, then everything is copied recursively except what skipped_in_export names. Returns `to`,
// or why it failed (and what state `to` is left in is unspecified).
[[nodiscard]] std::expected<std::filesystem::path, std::string> copy_export(
        const std::filesystem::path& from, const std::filesystem::path& to);

}
