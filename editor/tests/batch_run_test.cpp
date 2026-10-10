#include <gtest/gtest.h>

#include "batch_run.h"

#include <engine/process/process_launcher.h>

#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

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

void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// An SDK and a project under the temp directory, removed with the object.
class Dirs {
public:
    explicit Dirs(const std::string& project_engine = "0.1.0")
        : root_(fs::temp_directory_path() / "wind_batch_run_test" /
                  ::testing::UnitTest::GetInstance()->current_test_info()->name()) {
        fs::remove_all(root_);
        write(sdk() / "sdk.toml",
                "version = \"0.1.0\"\ncommit = \"abc\"\ndirty = false\nconfig = \"Release\"\nbuild_id = \"b\"\n");
        write(project() / "wind_project.toml",
                "name = \"My Game\"\nengine = \"" + project_engine + "\"\ntarget = \"my_game\"\n");
        // What the build would have made, and what the export record points at.
        write(built() / "my_game.exe", "exe");
        write(built() / "my_game.pdb", "symbols");
        write(built() / "assets" / "catalog.toml", "catalog");
        write(project() / "build-export" / "wind" / "my_game.Release.export", built().generic_string());
    }
    ~Dirs() {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    [[nodiscard]] fs::path sdk() const {
        return root_ / "sdk";
    }
    [[nodiscard]] fs::path project() const {
        return root_ / "game";
    }
    [[nodiscard]] fs::path built() const {
        return project() / "build-export" / "bin" / "Release";
    }
    [[nodiscard]] fs::path out() const {
        return root_ / "out";
    }

    [[nodiscard]] editor::BatchSetup setup(bool with_directory = true) const {
        return editor::BatchSetup{.sdk = sdk(),
                .project = project(),
                .export_directory = with_directory ? std::optional<fs::path>(out()) : std::nullopt};
    }

private:
    fs::path root_;
};

// Pumps the run the way main does, at most `ticks` times.
std::optional<int> run_to_end(editor::BatchRun& run, std::optional<int> code, int ticks = 10) {
    for (int i = 0; i < ticks && !code; ++i) {
        code = run.poll();
    }
    return code;
}

bool has_argument(const engine::ProcessDesc& desc, const std::string& argument) {
    return std::ranges::find(desc.arguments, argument) != desc.arguments.end();
}

}

TEST(BatchRun, ExportsAndCopiesTheGame) {
    const Dirs dirs;
    ScriptedLauncher launcher;
    launcher.answers.push_back({engine::ProcessExit{.code = 0}, {"-- Configuring done"}});
    launcher.answers.push_back({engine::ProcessExit{.code = 0}, {"my_game.vcxproj -> my_game.exe"}});
    std::vector<std::string> lines;
    editor::BatchRun run(launcher, [&](const std::string& line) { lines.push_back(line); });

    const std::optional<int> started = run.start(dirs.setup());
    EXPECT_FALSE(started.has_value());
    const std::optional<int> code = run_to_end(run, started);
    ASSERT_TRUE(code.has_value());
    EXPECT_EQ(*code, editor::kBatchOk);

    ASSERT_EQ(launcher.runs.size(), 2u);
    EXPECT_TRUE(has_argument(launcher.runs[0], "-DWIND_EXPORT=ON"));
    EXPECT_TRUE(has_argument(launcher.runs[1], "--build"));
    EXPECT_TRUE(has_argument(launcher.runs[1], "Release"));
    EXPECT_TRUE(fs::exists(dirs.out() / "my_game.exe"));
    EXPECT_TRUE(fs::exists(dirs.out() / "assets" / "catalog.toml"));
    EXPECT_FALSE(fs::exists(dirs.out() / "my_game.pdb"));

    EXPECT_TRUE(std::ranges::any_of(lines, [](const std::string& l) { return l == "-- Configuring done"; }));
    EXPECT_TRUE(std::ranges::any_of(lines, [](const std::string& l) { return l.starts_with("> cmake --build"); }));
    ASSERT_FALSE(lines.empty());
    EXPECT_TRUE(lines.back().starts_with("Exported to "));
    // Nothing more once it ended.
    EXPECT_FALSE(run.poll().has_value());
}

TEST(BatchRun, WithoutADirectoryTheGameGoesUnderTheProject) {
    const Dirs dirs;
    ScriptedLauncher launcher;
    editor::BatchRun run(launcher, [](const std::string&) {});
    const std::optional<int> code = run_to_end(run, run.start(dirs.setup(false)));
    ASSERT_TRUE(code.has_value());
    EXPECT_EQ(*code, editor::kBatchOk);
    EXPECT_TRUE(fs::exists(dirs.project() / "export" / "my_game" / "my_game.exe"));
}

TEST(BatchRun, AFailedConfigureEndsWithOne) {
    const Dirs dirs;
    ScriptedLauncher launcher;
    launcher.answers.push_back({engine::ProcessExit{.code = 1}, {"CMake Error at CMakeLists.txt:3 (find_package):"}});
    std::vector<std::string> lines;
    editor::BatchRun run(launcher, [&](const std::string& line) { lines.push_back(line); });
    const std::optional<int> code = run_to_end(run, run.start(dirs.setup()));
    ASSERT_TRUE(code.has_value());
    EXPECT_EQ(*code, editor::kBatchFailed);
    EXPECT_EQ(launcher.runs.size(), 1u);
    EXPECT_FALSE(fs::exists(dirs.out()));
    ASSERT_FALSE(lines.empty());
    EXPECT_TRUE(lines.back().starts_with("error: Configure failed"));
}

TEST(BatchRun, AFailedBuildEndsWithOneAndCopiesNothing) {
    const Dirs dirs;
    ScriptedLauncher launcher;
    launcher.answers.push_back({engine::ProcessExit{.code = 0}, {}});
    launcher.answers.push_back({engine::ProcessExit{.code = 1}, {"game.cpp(3,1): error C2065"}});
    editor::BatchRun run(launcher, [](const std::string&) {});
    const std::optional<int> code = run_to_end(run, run.start(dirs.setup()));
    ASSERT_TRUE(code.has_value());
    EXPECT_EQ(*code, editor::kBatchFailed);
    EXPECT_FALSE(fs::exists(dirs.out()));
}

TEST(BatchRun, AProjectForAnotherEngineVersionEndsWithOneWithoutBuilding) {
    const Dirs dirs("0.2.0");
    ScriptedLauncher launcher;
    std::vector<std::string> lines;
    editor::BatchRun run(launcher, [&](const std::string& line) { lines.push_back(line); });
    const std::optional<int> code = run.start(dirs.setup());
    ASSERT_TRUE(code.has_value());
    EXPECT_EQ(*code, editor::kBatchFailed);
    EXPECT_TRUE(launcher.runs.empty());
    ASSERT_FALSE(lines.empty());
    EXPECT_NE(lines.back().find("The project needs engine 0.2.0; this editor is 0.1.0."), std::string::npos);
}

TEST(BatchRun, AMissingProjectOrSdkEndsWithOne) {
    const Dirs dirs;
    ScriptedLauncher launcher;
    editor::BatchRun run(launcher, [](const std::string&) {});
    editor::BatchSetup setup = dirs.setup();
    setup.project = dirs.project() / "nope";
    EXPECT_EQ(run.start(setup), editor::kBatchFailed);

    setup = dirs.setup();
    setup.sdk = dirs.sdk() / "nope";
    EXPECT_EQ(run.start(setup), editor::kBatchFailed);
    EXPECT_TRUE(launcher.runs.empty());
}

TEST(BatchRun, ATargetThatWouldEmptyTheProjectIsRefused) {
    const Dirs dirs;
    ScriptedLauncher launcher;
    editor::BatchRun run(launcher, [](const std::string&) {});
    editor::BatchSetup setup = dirs.setup();
    setup.export_directory = dirs.project();
    EXPECT_EQ(run.start(setup), editor::kBatchFailed);
    EXPECT_TRUE(launcher.runs.empty());
    EXPECT_TRUE(fs::exists(dirs.project() / "wind_project.toml"));
}
