#pragma once

#include <engine/process/process_call.h>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace engine {
class IProcessLauncher;
}

namespace editor {

// What one Play builds: a project's game module against this editor's SDK.
struct BuildSetup {
    // The project root, where wind_project.toml and the game's CMakeLists.txt are.
    std::filesystem::path project;
    // This editor's SDK root (the directory with sdk.toml).
    std::filesystem::path sdk;
    // The engine_add_game target.
    std::string target;
    // The SDK's configuration (sdk.toml `config`): Release builds DebugGame, Debug builds Debug.
    std::string sdk_config;
};

// The module that was built, or why there is none.
using BuildOutcome = std::expected<std::filesystem::path, std::string>;

// <project>/build-editor: the build directory the editor configures and builds.
[[nodiscard]] std::filesystem::path build_directory(const std::filesystem::path& project);

// The game configuration the editor builds against an SDK of `sdk_config`, and the list it configures.
[[nodiscard]] std::string game_config(const std::string& sdk_config);
[[nodiscard]] std::string game_configurations(const std::string& sdk_config);

// True when `build_dir` has a CMake cache whose Wind_DIR is `<sdk>/cmake`, so it builds against this SDK without
// another configure.
[[nodiscard]] bool configured_for(const std::filesystem::path& build_dir, const std::filesystem::path& sdk);

// Builds a project's game module through cmake: configure when the build directory is not configured for this SDK,
// then `cmake --build` of the target. One ProcessCall at a time; the frame keeps running. Each step's command line
// and output come out of `poll` as lines.
class ProjectBuild {
public:
    explicit ProjectBuild(engine::IProcessLauncher& processes);

    ProjectBuild(const ProjectBuild&) = delete;
    ProjectBuild& operator=(const ProjectBuild&) = delete;

    // Starts a build. A build still running is cancelled first.
    void start(BuildSetup setup);

    // Ends the running step's process. No outcome follows.
    void cancel();

    [[nodiscard]] bool running() const;

    // Once per frame. Appends the lines that arrived to `lines`, and returns the outcome once, when the build ended.
    [[nodiscard]] std::optional<BuildOutcome> poll(std::vector<std::string>& lines);

private:
    enum class Step {
        Idle,
        Configure,
        Build,
    };

    void run(Step step, std::vector<std::string> arguments, std::vector<std::string>& lines);
    void start_build(std::vector<std::string>& lines);
    [[nodiscard]] BuildOutcome finish_build() const;

    engine::IProcessLauncher* processes_;
    BuildSetup setup_;
    Step step_ = Step::Idle;
    engine::ProcessCall call_;
    // Lines start() wrote before the first poll.
    std::vector<std::string> pending_lines_;
};

}
