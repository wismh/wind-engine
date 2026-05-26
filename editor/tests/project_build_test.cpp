#include <gtest/gtest.h>

#include "project_build.h"

#include <engine/process/process_launcher.h>

#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

// Records every run and answers each with the next scripted result and output.
class ScriptedLauncher final : public engine::IProcessLauncher {
public:
    struct Answer {
        engine::ProcessResult result;
        std::vector<std::string> output;
    };

    void dispose() override {}
    engine::ProcessCall run(engine::ProcessDesc desc) override {
        runs.push_back(std::move(desc));
        if (answers.empty()) {
            return engine::ProcessCall::resolved(engine::ProcessExit{.code = 0});
        }
        Answer answer = std::move(answers.front());
        answers.pop_front();
        return engine::ProcessCall::resolved(std::move(answer.result), std::move(answer.output));
    }
    std::expected<void, engine::ProcessError> launch(const engine::ProcessDesc&) override {
        return {};
    }
    bool is_supported() const override {
        return true;
    }

    std::deque<Answer> answers;
    std::vector<engine::ProcessDesc> runs;
};

// A project directory and an SDK directory under the temp directory, removed with the object.
class Dirs {
public:
    // One directory per test: ctest runs the tests of this file in parallel processes.
    Dirs()
        : root_(std::filesystem::temp_directory_path() / "wind_project_build_test" /
                  ::testing::UnitTest::GetInstance()->current_test_info()->name()) {
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(project() / "build-editor" / "wind");
        std::filesystem::create_directories(sdk() / "cmake");
        std::filesystem::create_directories(root_ / "other_sdk" / "cmake");
    }
    ~Dirs() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] std::filesystem::path project() const {
        return root_ / "game";
    }
    [[nodiscard]] std::filesystem::path sdk() const {
        return root_ / "sdk";
    }
    [[nodiscard]] std::filesystem::path other_sdk() const {
        return root_ / "other_sdk";
    }

    void cache_for(const std::filesystem::path& sdk, const std::string& generator = "Ninja Multi-Config") const {
        std::ofstream(project() / "build-editor" / "CMakeCache.txt")
                << "CMAKE_GENERATOR:INTERNAL=" << generator << "\n"
                << "CMAKE_CONFIGURATION_TYPES:STRING=DebugGame;Release\n"
                << "Wind_DIR:PATH=" << (sdk / "cmake").generic_string() << "\n";
    }
    void record_module(const std::string& config) const {
        std::ofstream(project() / "build-editor" / "wind" / ("my_game." + config + ".module"))
                << (project() / "build-editor" / "bin" / config / "my_game.dll").generic_string();
    }

    void export_cache_for(const std::filesystem::path& sdk, bool wind_export,
            const std::string& configurations = "DebugGame;Release") const {
        std::filesystem::create_directories(project() / "build-export");
        std::ofstream(project() / "build-export" / "CMakeCache.txt")
                << "CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config\n"
                << "CMAKE_CONFIGURATION_TYPES:STRING=" << configurations << "\n"
                << "WIND_EXPORT:BOOL=" << (wind_export ? "ON" : "OFF") << "\n"
                << "Wind_DIR:PATH=" << (sdk / "cmake").generic_string() << "\n";
    }
    void record_export(const std::string& config = "Release") const {
        std::filesystem::create_directories(project() / "build-export" / "wind");
        std::ofstream(project() / "build-export" / "wind" / ("my_game." + config + ".export"))
                << (project() / "build-export" / "bin" / config).generic_string() << "\n";
    }

    [[nodiscard]] editor::BuildSetup setup(std::string sdk_config = "Release") const {
        return editor::BuildSetup{.project = project(), .sdk = sdk(), .target = "my_game", .sdk_config = sdk_config};
    }
    [[nodiscard]] editor::BuildSetup export_setup(bool development = false, std::string sdk_config = "Release") const {
        return editor::BuildSetup{.kind = editor::BuildKind::Export,
                .project = project(),
                .sdk = sdk(),
                .target = "my_game",
                .sdk_config = std::move(sdk_config),
                .development = development};
    }

private:
    std::filesystem::path root_;
};

// Polls until the build ends, at most `frames` times.
std::optional<editor::BuildOutcome> run_to_end(
        editor::ProjectBuild& build, std::vector<std::string>& lines, int frames = 10) {
    for (int i = 0; i < frames; ++i) {
        if (std::optional<editor::BuildOutcome> outcome = build.poll(lines)) {
            return outcome;
        }
    }
    return std::nullopt;
}

bool has_argument(const engine::ProcessDesc& desc, const std::string& argument) {
    return std::ranges::find(desc.arguments, argument) != desc.arguments.end();
}

}

TEST(ProjectBuild, ConfiguresAFreshBuildDirectoryThenBuildsAndFindsTheModule) {
    const Dirs dirs;
    dirs.record_module("DebugGame");
    ScriptedLauncher launcher;
    launcher.answers.push_back({engine::ProcessExit{.code = 0}, {"-- Configuring done"}});
    launcher.answers.push_back({engine::ProcessExit{.code = 0}, {"my_game.vcxproj -> my_game.dll"}});
    editor::ProjectBuild build{launcher};
    build.start(dirs.setup());
    EXPECT_TRUE(build.running());

    std::vector<std::string> lines;
    const std::optional<editor::BuildOutcome> outcome = run_to_end(build, lines);
    ASSERT_TRUE(outcome.has_value());
    ASSERT_TRUE(outcome->has_value()) << outcome->error();
    EXPECT_EQ(**outcome, dirs.project() / "build-editor" / "bin" / "DebugGame" / "my_game.dll");
    EXPECT_FALSE(build.running());

    ASSERT_EQ(launcher.runs.size(), 2u);
    const engine::ProcessDesc& configure = launcher.runs[0];
    EXPECT_EQ(configure.program, "cmake");
    EXPECT_TRUE(has_argument(configure, "-S"));
    EXPECT_TRUE(has_argument(configure, "-G"));
    EXPECT_TRUE(has_argument(configure, "Ninja Multi-Config"));
    EXPECT_TRUE(has_argument(configure, "-DCMAKE_PREFIX_PATH=" + dirs.sdk().generic_string()));
    EXPECT_TRUE(has_argument(configure, "-DWind_DIR=" + (dirs.sdk() / "cmake").generic_string()));
    EXPECT_TRUE(has_argument(configure, "-DCMAKE_CONFIGURATION_TYPES=DebugGame;Release"));
    const engine::ProcessDesc& compile = launcher.runs[1];
    EXPECT_TRUE(has_argument(compile, "--build"));
    EXPECT_TRUE(has_argument(compile, "DebugGame"));
    EXPECT_TRUE(has_argument(compile, "my_game"));
    EXPECT_EQ(compile.working_directory, dirs.project());
    EXPECT_TRUE(compile.environment.empty());

    // Each step's command line, then its output.
    ASSERT_EQ(lines.size(), 4u);
    EXPECT_TRUE(lines[0].starts_with("> cmake -S "));
    EXPECT_EQ(lines[1], "-- Configuring done");
    EXPECT_TRUE(lines[2].starts_with("> cmake --build "));
    EXPECT_EQ(lines[3], "my_game.vcxproj -> my_game.dll");
}

TEST(ProjectBuild, ACacheForThisSdkSkipsConfigure) {
    const Dirs dirs;
    dirs.cache_for(dirs.sdk());
    dirs.record_module("DebugGame");
    ScriptedLauncher launcher;
    editor::ProjectBuild build{launcher};
    build.start(dirs.setup());
    std::vector<std::string> lines;
    const std::optional<editor::BuildOutcome> outcome = run_to_end(build, lines);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_TRUE(outcome->has_value());
    ASSERT_EQ(launcher.runs.size(), 1u);
    EXPECT_TRUE(has_argument(launcher.runs[0], "--build"));
}

TEST(ProjectBuild, AVisualStudioCacheIsDeletedAndConfiguredWithNinja) {
    const Dirs dirs;
    dirs.cache_for(dirs.sdk(), "Visual Studio 18 2026");
    EXPECT_FALSE(editor::configured_for(dirs.project() / "build-editor", dirs.sdk(), editor::BuildKind::Module,
            "DebugGame;Release"));
    ScriptedLauncher launcher;
    editor::ProjectBuild build{launcher};
    build.start(dirs.setup());
    EXPECT_FALSE(std::filesystem::exists(dirs.project() / "build-editor" / "CMakeCache.txt"));
    ASSERT_EQ(launcher.runs.size(), 1u);
    EXPECT_TRUE(has_argument(launcher.runs[0], "-G"));
    EXPECT_TRUE(has_argument(launcher.runs[0], "Ninja Multi-Config"));
}

TEST(ProjectBuild, TheSetupEnvironmentReachesCmake) {
    const Dirs dirs;
    dirs.cache_for(dirs.sdk());
    dirs.record_module("DebugGame");
    ScriptedLauncher launcher;
    editor::ProjectBuild build{launcher};
    editor::BuildSetup setup = dirs.setup();
    setup.environment.push_back(engine::ProcessVariable{"INCLUDE", "C:/inc"});
    build.start(std::move(setup));
    ASSERT_EQ(launcher.runs.size(), 1u);
    ASSERT_EQ(launcher.runs[0].environment.size(), 1u);
    EXPECT_EQ(launcher.runs[0].environment[0].name, "INCLUDE");
    EXPECT_EQ(launcher.runs[0].environment[0].value, "C:/inc");
}

TEST(ProjectBuild, ACacheForAnotherSdkConfiguresAgain) {
    const Dirs dirs;
    dirs.cache_for(dirs.other_sdk());
    const std::string configurations = "DebugGame;Release";
    EXPECT_FALSE(editor::configured_for(
            dirs.project() / "build-editor", dirs.sdk(), editor::BuildKind::Module, configurations));
    EXPECT_TRUE(editor::configured_for(
            dirs.project() / "build-editor", dirs.other_sdk(), editor::BuildKind::Module, configurations));
    // The same SDK with other configurations is another configure too.
    EXPECT_FALSE(editor::configured_for(
            dirs.project() / "build-editor", dirs.other_sdk(), editor::BuildKind::Module, "Debug"));
    dirs.record_module("DebugGame");
    ScriptedLauncher launcher;
    editor::ProjectBuild build{launcher};
    build.start(dirs.setup());
    std::vector<std::string> lines;
    ASSERT_TRUE(run_to_end(build, lines).has_value());
    ASSERT_EQ(launcher.runs.size(), 2u);
    EXPECT_TRUE(has_argument(launcher.runs[0], "-B"));
}

TEST(ProjectBuild, ADebugSdkBuildsDebug) {
    const Dirs dirs;
    dirs.record_module("Debug");
    ScriptedLauncher launcher;
    editor::ProjectBuild build{launcher};
    build.start(dirs.setup("Debug"));
    std::vector<std::string> lines;
    const std::optional<editor::BuildOutcome> outcome = run_to_end(build, lines);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_TRUE(outcome->has_value());
    EXPECT_TRUE(has_argument(launcher.runs[0], "-DCMAKE_CONFIGURATION_TYPES=Debug"));
    EXPECT_TRUE(has_argument(launcher.runs[1], "Debug"));
}

TEST(ProjectBuild, AFailedConfigureDoesNotBuild) {
    const Dirs dirs;
    ScriptedLauncher launcher;
    launcher.answers.push_back({engine::ProcessExit{.code = 1}, {"CMake Error at CMakeLists.txt:3 (find_package):"}});
    editor::ProjectBuild build{launcher};
    build.start(dirs.setup());
    std::vector<std::string> lines;
    const std::optional<editor::BuildOutcome> outcome = run_to_end(build, lines);
    ASSERT_TRUE(outcome.has_value());
    ASSERT_FALSE(outcome->has_value());
    EXPECT_NE(outcome->error().find("Configure failed"), std::string::npos);
    EXPECT_EQ(launcher.runs.size(), 1u);
}

TEST(ProjectBuild, AFailedBuildSaysSo) {
    const Dirs dirs;
    dirs.cache_for(dirs.sdk());
    dirs.record_module("DebugGame");
    ScriptedLauncher launcher;
    launcher.answers.push_back({engine::ProcessExit{.code = 1}, {"game.cpp(3,1): error C2065: 'x': undeclared"}});
    editor::ProjectBuild build{launcher};
    build.start(dirs.setup());
    std::vector<std::string> lines;
    const std::optional<editor::BuildOutcome> outcome = run_to_end(build, lines);
    ASSERT_TRUE(outcome.has_value());
    ASSERT_FALSE(outcome->has_value());
    EXPECT_NE(outcome->error().find("Build failed (exit code 1)"), std::string::npos);
}

TEST(ProjectBuild, MissingCmakeAndAMissingModuleRecordAreErrors) {
    const Dirs dirs;
    dirs.cache_for(dirs.sdk());
    ScriptedLauncher launcher;
    launcher.answers.push_back({std::unexpected(engine::ProcessError::NotFound), {}});
    editor::ProjectBuild build{launcher};
    build.start(dirs.setup());
    std::vector<std::string> lines;
    std::optional<editor::BuildOutcome> outcome = run_to_end(build, lines);
    ASSERT_TRUE(outcome.has_value());
    ASSERT_FALSE(outcome->has_value());
    EXPECT_NE(outcome->error().find("PATH"), std::string::npos);

    build.start(dirs.setup());
    outcome = run_to_end(build, lines);
    ASSERT_TRUE(outcome.has_value());
    ASSERT_FALSE(outcome->has_value());
    EXPECT_NE(outcome->error().find("recorded no module"), std::string::npos);
}

TEST(ProjectBuild, CancelEndsWithoutAnOutcome) {
    const Dirs dirs;
    ScriptedLauncher launcher;
    editor::ProjectBuild build{launcher};
    build.start(dirs.setup());
    build.cancel();
    EXPECT_FALSE(build.running());
    std::vector<std::string> lines;
    EXPECT_FALSE(run_to_end(build, lines).has_value());
    EXPECT_EQ(launcher.runs.size(), 1u);
}

TEST(ProjectBuild, ConfigurationsFollowTheSdk) {
    using editor::BuildKind;
    EXPECT_EQ(editor::game_config("Release", BuildKind::Module), "DebugGame");
    EXPECT_EQ(editor::game_configurations("Release", BuildKind::Module), "DebugGame;Release");
    EXPECT_EQ(editor::game_config("Debug", BuildKind::Module), "Debug");
    EXPECT_EQ(editor::game_configurations("Debug", BuildKind::Module), "Debug");
    // An export against a Release SDK is DebugGame (the Development build) or Release (Shipping) in one build
    // directory.
    EXPECT_EQ(editor::game_config("Release", BuildKind::Export), "Release");
    EXPECT_EQ(editor::game_config("Release", BuildKind::Export, true), "DebugGame");
    EXPECT_EQ(editor::game_configurations("Release", BuildKind::Export), "DebugGame;Release");
    // A Debug SDK has no player libraries, so its export is the Release build of the engine from source, whatever
    // was asked.
    EXPECT_EQ(editor::game_config("Debug", BuildKind::Export), "Release");
    EXPECT_EQ(editor::game_config("Debug", BuildKind::Export, true), "Release");
    EXPECT_EQ(editor::game_configurations("Debug", BuildKind::Export), "Release");
}

TEST(ProjectBuild, ADevelopmentBuildNeedsAReleaseSdk) {
    EXPECT_TRUE(editor::export_flavour_problem("Release", true).empty());
    EXPECT_TRUE(editor::export_flavour_problem("Release", false).empty());
    EXPECT_TRUE(editor::export_flavour_problem("Debug", false).empty());
    EXPECT_NE(editor::export_flavour_problem("Debug", true).find("Release SDK"), std::string::npos);
}

TEST(ProjectBuild, ExportConfiguresItsOwnDirectoryWithWindExportThenBuildsAndFindsTheDirectory) {
    const Dirs dirs;
    dirs.record_export();
    ScriptedLauncher launcher;
    launcher.answers.push_back({engine::ProcessExit{.code = 0}, {"-- Configuring done"}});
    launcher.answers.push_back({engine::ProcessExit{.code = 0}, {"my_game.vcxproj -> my_game.exe"}});
    editor::ProjectBuild build{launcher};
    build.start(dirs.export_setup());

    std::vector<std::string> lines;
    const std::optional<editor::BuildOutcome> outcome = run_to_end(build, lines);
    ASSERT_TRUE(outcome.has_value());
    ASSERT_TRUE(outcome->has_value()) << outcome->error();
    EXPECT_EQ(**outcome, dirs.project() / "build-export" / "bin" / "Release");

    ASSERT_EQ(launcher.runs.size(), 2u);
    const engine::ProcessDesc& configure = launcher.runs[0];
    EXPECT_TRUE(has_argument(configure, (dirs.project() / "build-export").generic_string()));
    EXPECT_FALSE(has_argument(configure, (dirs.project() / "build-editor").generic_string()));
    EXPECT_TRUE(has_argument(configure, "-DWIND_EXPORT=ON"));
    EXPECT_TRUE(has_argument(configure, "-G"));
    EXPECT_TRUE(has_argument(configure, "Ninja Multi-Config"));
    EXPECT_TRUE(has_argument(configure, "-DCMAKE_PREFIX_PATH=" + dirs.sdk().generic_string()));
    EXPECT_TRUE(has_argument(configure, "-DWind_DIR=" + (dirs.sdk() / "cmake").generic_string()));
    // One build directory holds both configurations; the build asks for Release (the Shipping player).
    EXPECT_TRUE(has_argument(configure, "-DCMAKE_CONFIGURATION_TYPES=DebugGame;Release"));
    const engine::ProcessDesc& compile = launcher.runs[1];
    EXPECT_TRUE(has_argument(compile, (dirs.project() / "build-export").generic_string()));
    EXPECT_TRUE(has_argument(compile, "Release"));
    EXPECT_FALSE(has_argument(compile, "DebugGame"));
    EXPECT_TRUE(has_argument(compile, "my_game"));
}

TEST(ProjectBuild, ADevelopmentExportBuildsDebugGameAndReadsItsRecord) {
    const Dirs dirs;
    dirs.record_export("DebugGame");
    ScriptedLauncher launcher;
    editor::ProjectBuild build{launcher};
    build.start(dirs.export_setup(true));

    std::vector<std::string> lines;
    const std::optional<editor::BuildOutcome> outcome = run_to_end(build, lines);
    ASSERT_TRUE(outcome.has_value());
    ASSERT_TRUE(outcome->has_value()) << outcome->error();
    EXPECT_EQ(**outcome, dirs.project() / "build-export" / "bin" / "DebugGame");

    ASSERT_EQ(launcher.runs.size(), 2u);
    EXPECT_TRUE(has_argument(launcher.runs[0], "-DWIND_EXPORT=ON"));
    EXPECT_TRUE(has_argument(launcher.runs[0], "-DCMAKE_CONFIGURATION_TYPES=DebugGame;Release"));
    EXPECT_TRUE(has_argument(launcher.runs[1], "DebugGame"));
    EXPECT_FALSE(has_argument(launcher.runs[1], "Release"));
}

TEST(ProjectBuild, TheDevelopmentFlagIsReleaseAgainstADebugSdk) {
    const Dirs dirs;
    dirs.record_export("Release");
    ScriptedLauncher launcher;
    editor::ProjectBuild build{launcher};
    build.start(dirs.export_setup(true, "Debug"));
    std::vector<std::string> lines;
    const std::optional<editor::BuildOutcome> outcome = run_to_end(build, lines);
    ASSERT_TRUE(outcome.has_value());
    ASSERT_TRUE(outcome->has_value()) << outcome->error();
    EXPECT_TRUE(has_argument(launcher.runs[0], "-DCMAKE_CONFIGURATION_TYPES=Release"));
    EXPECT_TRUE(has_argument(launcher.runs[1], "Release"));
}

TEST(ProjectBuild, BothFlavoursShareOneConfiguredBuildDirectory) {
    const Dirs dirs;
    dirs.export_cache_for(dirs.sdk(), true);
    dirs.record_export("DebugGame");
    dirs.record_export("Release");
    ScriptedLauncher launcher;
    editor::ProjectBuild build{launcher};
    std::vector<std::string> lines;
    for (const bool development : {true, false}) {
        build.start(dirs.export_setup(development));
        const std::optional<editor::BuildOutcome> outcome = run_to_end(build, lines);
        ASSERT_TRUE(outcome.has_value());
        ASSERT_TRUE(outcome->has_value()) << outcome->error();
    }
    // No configure for either: the cache already holds DebugGame;Release.
    ASSERT_EQ(launcher.runs.size(), 2u);
    EXPECT_TRUE(has_argument(launcher.runs[0], "DebugGame"));
    EXPECT_TRUE(has_argument(launcher.runs[1], "Release"));
}

TEST(ProjectBuild, ModuleBuildsDoNotAskForAnExport) {
    const Dirs dirs;
    dirs.record_module("DebugGame");
    ScriptedLauncher launcher;
    editor::ProjectBuild build{launcher};
    build.start(dirs.setup());
    std::vector<std::string> lines;
    ASSERT_TRUE(run_to_end(build, lines).has_value());
    EXPECT_FALSE(has_argument(launcher.runs[0], "-DWIND_EXPORT=ON"));
}

TEST(ProjectBuild, AnExportCacheForThisSdkWithWindExportSkipsConfigure) {
    const Dirs dirs;
    dirs.export_cache_for(dirs.sdk(), true);
    EXPECT_TRUE(editor::configured_for(
            dirs.project() / "build-export", dirs.sdk(), editor::BuildKind::Export, "DebugGame;Release"));
    dirs.record_export();
    ScriptedLauncher launcher;
    editor::ProjectBuild build{launcher};
    build.start(dirs.export_setup());
    std::vector<std::string> lines;
    const std::optional<editor::BuildOutcome> outcome = run_to_end(build, lines);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_TRUE(outcome->has_value());
    ASSERT_EQ(launcher.runs.size(), 1u);
    EXPECT_TRUE(has_argument(launcher.runs[0], "--build"));
}

TEST(ProjectBuild, AnExportCacheWithoutWindExportOrForAnotherSdkConfiguresAgain) {
    const Dirs dirs;
    const std::string configurations = "DebugGame;Release";
    dirs.export_cache_for(dirs.sdk(), false);
    EXPECT_FALSE(editor::configured_for(
            dirs.project() / "build-export", dirs.sdk(), editor::BuildKind::Export, configurations));
    dirs.export_cache_for(dirs.other_sdk(), true);
    EXPECT_FALSE(editor::configured_for(
            dirs.project() / "build-export", dirs.sdk(), editor::BuildKind::Export, configurations));
    // A build directory an earlier editor configured for Release only cannot build DebugGame.
    dirs.export_cache_for(dirs.sdk(), true, "Release");
    EXPECT_FALSE(editor::configured_for(
            dirs.project() / "build-export", dirs.sdk(), editor::BuildKind::Export, configurations));
    dirs.export_cache_for(dirs.other_sdk(), true);
    dirs.record_export();
    ScriptedLauncher launcher;
    editor::ProjectBuild build{launcher};
    build.start(dirs.export_setup());
    std::vector<std::string> lines;
    ASSERT_TRUE(run_to_end(build, lines).has_value());
    ASSERT_EQ(launcher.runs.size(), 2u);
    EXPECT_TRUE(has_argument(launcher.runs[0], "-DWIND_EXPORT=ON"));
}

TEST(ProjectBuild, ExportFailuresAreReportedAndAMissingRecordIsAnError) {
    const Dirs dirs;
    ScriptedLauncher launcher;
    launcher.answers.push_back({engine::ProcessExit{.code = 1}, {"CMake Error: WIND_EXPORT needs source/"}});
    editor::ProjectBuild build{launcher};
    build.start(dirs.export_setup());
    std::vector<std::string> lines;
    std::optional<editor::BuildOutcome> outcome = run_to_end(build, lines);
    ASSERT_TRUE(outcome.has_value());
    ASSERT_FALSE(outcome->has_value());
    EXPECT_NE(outcome->error().find("Configure failed"), std::string::npos);
    EXPECT_EQ(launcher.runs.size(), 1u);

    // Configure and build pass, but the target recorded no executable.
    build.start(dirs.export_setup());
    outcome = run_to_end(build, lines);
    ASSERT_TRUE(outcome.has_value());
    ASSERT_FALSE(outcome->has_value());
    EXPECT_NE(outcome->error().find("recorded no executable"), std::string::npos);
}
