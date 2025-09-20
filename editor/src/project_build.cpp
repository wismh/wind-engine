#include "project_build.h"

#include <engine/log.h>
#include <engine/process/process_launcher.h>

#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <utility>

namespace editor {
namespace {

std::string path_text(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::filesystem::path path_from(std::string_view text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

// MSBuild prints English messages, so the Build tab reads the same on every machine, and starts no node that would
// outlive the build: the job of the call ends such nodes anyway.
const std::vector<engine::ProcessVariable> kBuildEnvironment{
        {"VSLANG", "1033"},
        {"MSBUILDDISABLENODEREUSE", "1"},
};

std::string command_text(const std::vector<std::string>& arguments) {
    std::string text = "> cmake";
    for (const std::string& argument : arguments) {
        text += ' ';
        text += argument.find(' ') == std::string::npos ? argument : '"' + argument + '"';
    }
    return text;
}

std::string error_text(engine::ProcessError error) {
    switch (error) {
        case engine::ProcessError::NotFound:
            return "CMake was not found. Install CMake and put it on PATH.";
        case engine::ProcessError::StartFailed:
            return "CMake could not start.";
        case engine::ProcessError::Unsupported:
            return "Building is not supported on this platform.";
    }
    return "CMake could not start.";
}

void append(std::vector<std::string>& into, std::vector<std::string> lines) {
    into.insert(into.end(), std::make_move_iterator(lines.begin()), std::make_move_iterator(lines.end()));
}

}

std::filesystem::path build_directory(const std::filesystem::path& project, BuildKind kind) {
    return project / (kind == BuildKind::Export ? "build-export" : "build-editor");
}

std::string game_config(const std::string& sdk_config, BuildKind kind) {
    if (kind == BuildKind::Export) {
        return "Release";
    }
    return sdk_config == "Debug" ? "Debug" : "DebugGame";
}

std::string game_configurations(const std::string& sdk_config, BuildKind kind) {
    if (kind == BuildKind::Export) {
        return "Release";
    }
    return sdk_config == "Debug" ? "Debug" : "DebugGame;Release";
}

bool configured_for(const std::filesystem::path& build_dir, const std::filesystem::path& sdk, BuildKind kind) {
    std::ifstream cache(build_dir / "CMakeCache.txt");
    constexpr std::string_view kSdkKey = "Wind_DIR:";
    constexpr std::string_view kExportKey = "WIND_EXPORT:";
    bool sdk_matches = false;
    bool export_on = false;
    for (std::string line; std::getline(cache, line);) {
        const bool is_sdk = line.starts_with(kSdkKey);
        const bool is_export = line.starts_with(kExportKey);
        if (!is_sdk && !is_export) {
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string_view value = std::string_view(line).substr(equals + 1);
        if (is_sdk) {
            std::error_code error;
            sdk_matches = std::filesystem::equivalent(path_from(value), sdk / "cmake", error);
        } else {
            export_on = value == "ON" || value == "TRUE" || value == "1" || value == "YES" || value == "Y";
        }
    }
    return sdk_matches && (kind == BuildKind::Module || export_on);
}

ProjectBuild::ProjectBuild(engine::IProcessLauncher& processes)
    : processes_(&processes) {}

void ProjectBuild::start(BuildSetup setup) {
    cancel();
    setup_ = std::move(setup);
    pending_lines_.clear();
    const std::filesystem::path build_dir = build_directory(setup_.project, setup_.kind);
    if (configured_for(build_dir, setup_.sdk, setup_.kind)) {
        start_build(pending_lines_);
        return;
    }
    std::vector<std::string> arguments{"-S", path_text(setup_.project), "-B", path_text(build_dir)};
#if !defined(_WIN32)
    // CMake's default here is single-configuration Makefiles, which cannot hold DebugGame and Release together.
    // (On Windows the default is Visual Studio, which can.) A build directory that exists keeps its generator.
    arguments.insert(arguments.end(), {"-G", "Ninja Multi-Config"});
#endif
    if (setup_.kind == BuildKind::Export) {
        // WindConfig.cmake builds the static engine from <sdk>/source for this game (docs/tech/build/CMake.md).
        arguments.push_back("-DWIND_EXPORT=ON");
    }
    arguments.insert(arguments.end(),
            {"-DCMAKE_PREFIX_PATH=" + path_text(setup_.sdk), "-DWind_DIR=" + path_text(setup_.sdk / "cmake"),
                    "-DCMAKE_CONFIGURATION_TYPES=" + game_configurations(setup_.sdk_config, setup_.kind)});
    run(Step::Configure, std::move(arguments), pending_lines_);
}

void ProjectBuild::cancel() {
    call_.cancel();
    call_ = engine::ProcessCall{};
    step_ = Step::Idle;
}

bool ProjectBuild::running() const {
    return step_ != Step::Idle;
}

std::optional<BuildOutcome> ProjectBuild::poll(std::vector<std::string>& lines) {
    append(lines, std::exchange(pending_lines_, {}));
    if (step_ == Step::Idle) {
        return std::nullopt;
    }
    append(lines, call_.take_output());
    const std::optional<engine::ProcessResult> result = call_.take();
    if (!result) {
        return std::nullopt;
    }
    const Step finished = std::exchange(step_, Step::Idle);
    call_ = engine::ProcessCall{};
    if (!result->has_value()) {
        return std::unexpected(error_text(result->error()));
    }
    const int code = (*result)->code;
    if (finished == Step::Configure) {
        if (code != 0) {
            return std::unexpected("Configure failed (exit code " + std::to_string(code) + "). See the Build tab.");
        }
        start_build(lines);
        return std::nullopt;
    }
    if (code != 0) {
        return std::unexpected("Build failed (exit code " + std::to_string(code) + "). See the Build tab.");
    }
    return finish_build();
}

void ProjectBuild::run(Step step, std::vector<std::string> arguments, std::vector<std::string>& lines) {
    const std::string command = command_text(arguments);
    engine::log::info("Editor: " + command.substr(2));
    lines.push_back(command);
    step_ = step;
    call_ = processes_->run(engine::ProcessDesc{
            .program = "cmake",
            .arguments = std::move(arguments),
            .working_directory = setup_.project,
            .environment = kBuildEnvironment,
    });
}

void ProjectBuild::start_build(std::vector<std::string>& lines) {
    run(Step::Build,
            {"--build", path_text(build_directory(setup_.project, setup_.kind)), "--config", config(), "--target",
                    setup_.target, "--parallel"},
            lines);
}

std::string ProjectBuild::config() const {
    return game_config(setup_.sdk_config, setup_.kind);
}

BuildOutcome ProjectBuild::finish_build() const {
    const bool exporting = setup_.kind == BuildKind::Export;
    const std::filesystem::path record = build_directory(setup_.project, setup_.kind) / "wind" /
            (setup_.target + "." + config() + (exporting ? ".export" : ".module"));
    std::ifstream in(record, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    if (text.empty()) {
        return std::unexpected("The build recorded no " + std::string(exporting ? "executable" : "module") + " for " +
                setup_.target + " (" + path_text(record) + "). Is it an engine_add_game target of this project?");
    }
    return path_from(text);
}

}
