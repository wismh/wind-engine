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

// What a build makes.
enum class BuildKind {
    // The game module Play loads, in <project>/build-editor, against the SDK's shared engine.
    Module,
    // The standalone executable Export copies, in <project>/build-export (-DWIND_EXPORT=ON): the game linked with the
    // SDK's prebuilt static engine, the Development player in DebugGame and the Shipping player in Release. An SDK
    // without player libraries (a Debug SDK) builds the engine from <sdk>/source instead, in Release only.
    Export,
};

// What one build makes: a project's game module against this editor's SDK, or its standalone executable.
struct BuildSetup {
    BuildKind kind = BuildKind::Module;
    // The project root, where wind_project.toml and the game's CMakeLists.txt are.
    std::filesystem::path project;
    // This editor's SDK root (the directory with sdk.toml).
    std::filesystem::path sdk;
    // The engine_add_game target.
    std::string target;
    // The SDK's configuration (sdk.toml `config`): Release builds DebugGame, Debug builds Debug. An Export builds
    // DebugGame (`development`) or Release against a Release SDK, and Release against a Debug SDK.
    std::string sdk_config;
    // An Export only: the Development build (the game's DebugGame configuration, linked with the Development player:
    // unoptimized game code with symbols, the UI profiler, and the CLI server) instead of Release (Shipping player).
    bool development = false;
    // Extra environment for both cmake steps. On Windows the editor passes the MSVC developer environment, so `cl`
    // and Ninja are found when the editor itself was not started from a developer prompt. Empty keeps the parent's.
    std::vector<engine::ProcessVariable> environment;
};

// What was built: the game module (Module), or the directory with the executable and its assets/ (Export); or why
// there is none.
using BuildOutcome = std::expected<std::filesystem::path, std::string>;

// <project>/build-editor (Module) or <project>/build-export (Export): the build directory the editor configures and
// builds. The two never share a directory: their caches differ in what Wind_DIR leads to.
[[nodiscard]] std::filesystem::path build_directory(const std::filesystem::path& project, BuildKind kind);

// The game configuration the editor builds against an SDK of `sdk_config`, and the list it configures. A Module is
// DebugGame (Debug against a Debug SDK). An Export is DebugGame when `development`, else Release; against a Debug SDK
// it is always Release, because that SDK has no Development player (export_flavour_problem).
[[nodiscard]] std::string game_config(const std::string& sdk_config, BuildKind kind, bool development = false);
[[nodiscard]] std::string game_configurations(const std::string& sdk_config, BuildKind kind);

// Why the Development build cannot be exported against an SDK of `sdk_config` (a Debug SDK ships no player libraries,
// so its Export is the engine built from source, Release only), or empty.
[[nodiscard]] std::string export_flavour_problem(const std::string& sdk_config, bool development);

// True when `build_dir` has a CMake cache whose generator is Ninja Multi-Config, whose Wind_DIR is `<sdk>/cmake`,
// whose CMAKE_CONFIGURATION_TYPES is `configurations` (game_configurations), and (for an Export) whose WIND_EXPORT is
// ON, so it builds against this SDK without another configure.
[[nodiscard]] bool configured_for(const std::filesystem::path& build_dir, const std::filesystem::path& sdk,
        BuildKind kind, const std::string& configurations);

// Builds a project's game module or standalone executable through cmake: configure when the build directory is not
// configured for this SDK, then `cmake --build` of the target. One ProcessCall at a time; the frame keeps running.
// Each step's command line and output come out of `poll` as lines.
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
    [[nodiscard]] std::string config() const;

    engine::IProcessLauncher* processes_;
    BuildSetup setup_;
    Step step_ = Step::Idle;
    engine::ProcessCall call_;
    // Lines start() wrote before the first poll.
    std::vector<std::string> pending_lines_;
    // Set when start() could not clear a build directory made by another generator. poll returns it once.
    std::optional<std::string> start_error_;
};

}
