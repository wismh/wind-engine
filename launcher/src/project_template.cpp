#include "project_template.h"

#include "launcher_state.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace launcher {
namespace {

bool ascii_alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

// Names Windows keeps for devices, with or without an extension.
bool reserved_name(std::string_view name) {
    constexpr std::array<std::string_view, 4> kDevices{"con", "prn", "aux", "nul"};
    std::string stem(name.substr(0, name.find('.')));
    std::ranges::transform(
            stem, stem.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    if (std::ranges::find(kDevices, stem) != kDevices.end()) {
        return true;
    }
    return stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9';
}

std::string replace_all(std::string text, std::string_view from, std::string_view to) {
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
    return text;
}

bool empty_or_missing(const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::exists(directory, error)) {
        return true;
    }
    return std::filesystem::is_directory(directory, error) && std::filesystem::is_empty(directory, error);
}

}

std::filesystem::path project_template(const SdkEntry& sdk) {
    return sdk.root / "templates" / "empty";
}

std::string project_target(std::string_view name) {
    std::string target;
    bool dash = false;
    for (const char c : name) {
        if (ascii_alnum(c)) {
            if (dash && !target.empty()) {
                target += '-';
            }
            dash = false;
            target += c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        } else {
            dash = true;
        }
    }
    if (!target.empty() && target.front() >= '0' && target.front() <= '9') {
        target.insert(0, "game-");
    }
    return target;
}

std::filesystem::path project_directory(const NewProject& project) {
    return project.location / path_from_text(project.name);
}

std::optional<std::string> new_project_problem(const NewProject& project) {
    const std::string_view name = project.name;
    if (name.empty()) {
        return "Name the project.";
    }
    for (const char c : name) {
        if (static_cast<unsigned char>(c) < 0x20 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos) {
            return "A project name cannot contain < > : \" / \\ | ? *.";
        }
    }
    if (name.front() == ' ' || name.back() == ' ' || name.back() == '.') {
        return "A project name cannot start with a space or end with a space or a dot.";
    }
    if (reserved_name(name)) {
        return "Windows keeps that name for a device. Pick another.";
    }
    if (project_target(name).empty()) {
        return "Put a Latin letter or a digit in the name: the build target is made from them.";
    }
    if (project.location.empty()) {
        return "Pick where the project goes.";
    }
    if (!project.location.is_absolute()) {
        return "The location must be a full path, such as C:/Projects.";
    }
#if defined(_WIN32)
    // MSBuild writes files about 150 characters deep under build-editor/ and fails past MAX_PATH (260).
    if (const std::size_t length = path_text(project_directory(project)).size(); length > kMaxProjectPath) {
        return "MSBuild cannot build under a path this long (" + std::to_string(length) + " characters). Keep it to " +
               std::to_string(kMaxProjectPath) + ": pick a shorter location or name.";
    }
#endif
    if (!empty_or_missing(project_directory(project))) {
        return path_text(project_directory(project)) + " already exists and is not empty.";
    }
    return std::nullopt;
}

std::expected<std::filesystem::path, std::string> create_project(
        const std::filesystem::path& template_dir, const NewProject& project) {
    if (const std::optional<std::string> problem = new_project_problem(project)) {
        return std::unexpected(*problem);
    }
    std::error_code error;
    if (!std::filesystem::is_directory(template_dir, error)) {
        return std::unexpected("The SDK has no project template (" + path_text(template_dir) +
                "). Install an SDK built from a newer engine.");
    }

    const std::filesystem::path directory = project_directory(project);
    const bool made_directory = !std::filesystem::exists(directory, error);
    const auto fail = [&](std::string why) -> std::expected<std::filesystem::path, std::string> {
        std::error_code ignored;
        if (made_directory) {
            std::filesystem::remove_all(directory, ignored);
        }
        return std::unexpected("Could not create " + path_text(directory) + ": " + why);
    };

    std::vector<std::filesystem::path> files;
    for (std::filesystem::recursive_directory_iterator it(template_dir, error), end; !error && it != end;
            it.increment(error)) {
        if (it->is_regular_file(error)) {
            files.push_back(it->path());
        }
    }
    if (error) {
        return fail("the template cannot be read (" + error.message() + ").");
    }

    const std::string target = project_target(project.name);
    const std::string sdk = path_text(project.sdk_root);
    for (const std::filesystem::path& file : files) {
        std::ifstream in(file, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!in && !in.eof()) {
            return fail(path_text(file) + " cannot be read.");
        }
        text = replace_all(std::move(text), "{{name}}", project.name);
        text = replace_all(std::move(text), "{{target}}", target);
        text = replace_all(std::move(text), "{{engine}}", project.engine);
        text = replace_all(std::move(text), "{{sdk}}", sdk);

        const std::filesystem::path out_path = directory / file.lexically_relative(template_dir);
        std::filesystem::create_directories(out_path.parent_path(), error);
        if (error) {
            return fail(error.message() + ".");
        }
        std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) {
            return fail(path_text(out_path) + " cannot be written.");
        }
    }
    return directory;
}

}
