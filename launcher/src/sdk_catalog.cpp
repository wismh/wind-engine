#include "sdk_catalog.h"

#include "launcher_state.h"

#include <algorithm>
#include <charconv>
#include <system_error>

namespace launcher {
namespace {

std::string_view next_part(std::string_view& version) {
    const std::size_t dot = version.find('.');
    const std::string_view part = version.substr(0, dot);
    version = dot == std::string_view::npos ? std::string_view{} : version.substr(dot + 1);
    return part;
}

bool known(const std::vector<SdkEntry>& sdks, const std::filesystem::path& root) {
    return std::ranges::any_of(sdks, [&](const SdkEntry& entry) { return same_directory(entry.root, root); });
}

}

int compare_versions(std::string_view a, std::string_view b) {
    while (!a.empty() || !b.empty()) {
        const std::string_view left = next_part(a);
        const std::string_view right = next_part(b);
        unsigned long long left_number = 0;
        unsigned long long right_number = 0;
        const auto [left_end, left_error] = std::from_chars(left.data(), left.data() + left.size(), left_number);
        const auto [right_end, right_error] = std::from_chars(right.data(), right.data() + right.size(), right_number);
        const bool numbers = left_error == std::errc{} && right_error == std::errc{} &&
                             left_end == left.data() + left.size() && right_end == right.data() + right.size();
        if (numbers) {
            if (left_number != right_number) {
                return left_number < right_number ? -1 : 1;
            }
        } else if (const int text = left.compare(right); text != 0) {
            return text < 0 ? -1 : 1;
        }
    }
    return 0;
}

std::vector<SdkEntry> find_sdks(const std::filesystem::path& install_dir,
        const std::vector<std::filesystem::path>& located, std::vector<std::string>& problems) {
    std::vector<SdkEntry> sdks;
    std::error_code error;
    if (!install_dir.empty() && std::filesystem::is_directory(install_dir, error)) {
        for (const std::filesystem::directory_entry& child : std::filesystem::directory_iterator(install_dir, error)) {
            if (!child.is_directory(error) || !std::filesystem::exists(child.path() / engine::kSdkManifestFile, error)) {
                continue;
            }
            if (auto manifest = engine::read_sdk_manifest(child.path())) {
                sdks.push_back(SdkEntry{.root = child.path(), .manifest = std::move(*manifest), .located = false});
            } else {
                problems.push_back(path_text(child.path()) + ": " + engine::describe(manifest.error()));
            }
        }
    }
    for (const std::filesystem::path& root : located) {
        if (known(sdks, root)) {
            continue;
        }
        if (auto manifest = engine::read_sdk_manifest(root)) {
            sdks.push_back(SdkEntry{.root = root, .manifest = std::move(*manifest), .located = true});
        } else {
            problems.push_back(path_text(root) + ": " + engine::describe(manifest.error()));
        }
    }
    std::ranges::stable_sort(sdks, [](const SdkEntry& a, const SdkEntry& b) {
        if (const int order = compare_versions(a.manifest.version, b.manifest.version); order != 0) {
            return order > 0;
        }
        return !a.manifest.dirty && b.manifest.dirty;
    });
    return sdks;
}

const SdkEntry* sdk_for(const std::vector<SdkEntry>& sdks, std::string_view version) {
    const auto found = std::ranges::find_if(sdks, [&](const SdkEntry& entry) { return entry.manifest.version == version; });
    return found == sdks.end() ? nullptr : &*found;
}

std::filesystem::path editor_executable(const SdkEntry& sdk) {
#if defined(_WIN32)
    return sdk.root / "bin" / "wind_editor.exe";
#else
    return sdk.root / "bin" / "wind_editor";
#endif
}

engine::ProcessDesc editor_launch(const SdkEntry& sdk, const std::filesystem::path& project) {
    return engine::ProcessDesc{
            .program = editor_executable(sdk),
            .arguments = {"--project", path_text(project)},
            .working_directory = sdk.root / "bin",
            .environment = {},
    };
}

}
