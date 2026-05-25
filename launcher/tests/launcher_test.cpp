#include <gtest/gtest.h>

#include "launcher_state.h"
#include "project_entry.h"
#include "sdk_catalog.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

// A fresh directory per test under the temp directory, removed with the object. ctest runs tests in parallel.
class TempDir {
public:
    TempDir()
        : path_(std::filesystem::temp_directory_path() / "wind_launcher_test" /
                  ::testing::UnitTest::GetInstance()->current_test_info()->name()) {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

    void write(const std::filesystem::path& relative, std::string_view text) const {
        std::filesystem::create_directories((path_ / relative).parent_path());
        std::ofstream(path_ / relative, std::ios::binary) << text;
    }

    void sdk(const std::filesystem::path& relative, std::string_view version, bool dirty) const {
        write(relative / "sdk.toml", "version = \"" + std::string(version) + "\"\ncommit = \"0123456789abcdef\"\n" +
                                             "dirty = " + (dirty ? "true" : "false") +
                                             "\nconfig = \"Release\"\nbuild_id = \"aa\"\n");
    }

private:
    std::filesystem::path path_;
};

}

TEST(LauncherState, ParsesProjectsAndSdksAndSkipsTheRest) {
    const launcher::LauncherState state = launcher::parse_launcher_state(
            "# comment\r\nproject=C:/games/ttt\r\nsdk=C:/engine/out/sdk\nwindow=10,10\n\nproject=C:/games/emf\nproject=\n");
    ASSERT_EQ(state.projects.size(), 2u);
    EXPECT_EQ(state.projects[0], std::filesystem::path("C:/games/ttt"));
    EXPECT_EQ(state.projects[1], std::filesystem::path("C:/games/emf"));
    ASSERT_EQ(state.sdks.size(), 1u);
    EXPECT_EQ(state.sdks[0], std::filesystem::path("C:/engine/out/sdk"));
}

TEST(LauncherState, FormatsWhatItParses) {
    launcher::LauncherState state;
    state.projects = {std::filesystem::path(u8"C:/ігри/ttt"), "D:/emf"};
    state.sdks = {"C:/engine/out/sdk"};
    const launcher::LauncherState again = launcher::parse_launcher_state(launcher::format_launcher_state(state));
    EXPECT_EQ(again.projects, state.projects);
    EXPECT_EQ(again.sdks, state.sdks);
}

TEST(LauncherState, SavesAndLoadsAndAMissingFileIsEmpty) {
    const TempDir dir;
    EXPECT_TRUE(launcher::load_launcher_state(dir.path() / "launcher.txt").projects.empty());
    launcher::LauncherState state;
    state.projects = {"C:/games/ttt"};
    ASSERT_TRUE(launcher::save_launcher_state(dir.path() / "launcher.txt", state));
    EXPECT_EQ(launcher::load_launcher_state(dir.path() / "launcher.txt").projects, state.projects);
}

TEST(LauncherState, RememberPutsTheProjectFirstOnce) {
    launcher::LauncherState state;
    launcher::remember_project(state, "C:/games/a");
    launcher::remember_project(state, "C:/games/b");
    launcher::remember_project(state, "C:/games/a/");
    ASSERT_EQ(state.projects.size(), 2u);
    EXPECT_TRUE(launcher::same_directory(state.projects[0], "C:/games/a"));
    EXPECT_TRUE(launcher::same_directory(state.projects[1], "C:/games/b"));
    launcher::forget_project(state, "C:/games/b");
    EXPECT_EQ(state.projects.size(), 1u);
#if defined(_WIN32)
    EXPECT_TRUE(launcher::same_directory("C:/Games/A", "c:\\games\\a"));
#endif
}

TEST(LauncherState, RememberSdkKeepsOrderWithoutDuplicates) {
    launcher::LauncherState state;
    launcher::remember_sdk(state, "C:/sdk/one");
    launcher::remember_sdk(state, "C:/sdk/two");
    launcher::remember_sdk(state, "C:/sdk/one/");
    ASSERT_EQ(state.sdks.size(), 2u);
    EXPECT_EQ(state.sdks[0], std::filesystem::path("C:/sdk/one"));
    launcher::forget_sdk(state, "C:/sdk/one");
    ASSERT_EQ(state.sdks.size(), 1u);
    EXPECT_EQ(state.sdks[0], std::filesystem::path("C:/sdk/two"));
}

TEST(SdkCatalog, ComparesVersionsByNumber) {
    EXPECT_GT(launcher::compare_versions("0.10.0", "0.9.1"), 0);
    EXPECT_LT(launcher::compare_versions("0.1.0", "0.1.1"), 0);
    EXPECT_EQ(launcher::compare_versions("1.2.3", "1.2.3"), 0);
    EXPECT_GT(launcher::compare_versions("10.0.0", "2.0.0"), 0);
    EXPECT_GT(launcher::compare_versions("1.0.0-rc", "1.0.0-beta"), 0) << "a part that is not a number compares as text";
}

TEST(SdkCatalog, FindsInstalledAndLocatedSdksNewestFirstCleanBeforeDirty) {
    const TempDir dir;
    dir.sdk("sdks/0.1.0-dev", "0.1.0", true);
    dir.sdk("sdks/0.2.0", "0.2.0", false);
    dir.write("sdks/notes/readme.txt", "not an sdk");
    dir.sdk("dev/sdk", "0.1.0", false);
    dir.write("broken/sdk.toml", "version = \n");

    std::vector<std::string> problems;
    const std::vector<launcher::SdkEntry> sdks = launcher::find_sdks(dir.path() / "sdks",
            {dir.path() / "dev" / "sdk", dir.path() / "broken", dir.path() / "sdks" / "0.2.0"}, problems);
    ASSERT_EQ(sdks.size(), 3u);
    EXPECT_EQ(sdks[0].manifest.version, "0.2.0");
    EXPECT_FALSE(sdks[0].located) << "a located root that is also installed is listed once, as installed";
    EXPECT_EQ(sdks[1].manifest.version, "0.1.0");
    EXPECT_FALSE(sdks[1].manifest.dirty);
    EXPECT_TRUE(sdks[1].located);
    EXPECT_TRUE(sdks[2].manifest.dirty);
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_NE(problems[0].find("broken"), std::string::npos);

    const launcher::SdkEntry* match = launcher::sdk_for(sdks, "0.1.0");
    ASSERT_NE(match, nullptr);
    EXPECT_FALSE(match->manifest.dirty);
    EXPECT_EQ(launcher::sdk_for(sdks, "0.3.0"), nullptr);
}

TEST(SdkCatalog, NoInstallDirectoryFindsOnlyLocatedSdks) {
    const TempDir dir;
    dir.sdk("dev", "0.1.0", false);
    std::vector<std::string> problems;
    const auto sdks = launcher::find_sdks({}, {dir.path() / "dev"}, problems);
    ASSERT_EQ(sdks.size(), 1u);
    EXPECT_TRUE(problems.empty());
}

TEST(SdkCatalog, TheEditorStartsFromItsBinWithTheProject) {
    const launcher::SdkEntry sdk{.root = "C:/sdk", .manifest = {}, .located = true};
    const engine::ProcessDesc desc = launcher::editor_launch(sdk, "C:/games/ttt");
    EXPECT_EQ(desc.program.filename().stem(), "wind_editor");
    EXPECT_EQ(desc.program.parent_path(), std::filesystem::path("C:/sdk/bin"));
    EXPECT_EQ(desc.working_directory, std::filesystem::path("C:/sdk/bin"));
    EXPECT_EQ(desc.arguments, (std::vector<std::string>{"--project", "C:/games/ttt"}));
}

TEST(ProjectEntry, ReadsTheProjectOrSaysWhatIsWrong) {
    const TempDir dir;
    dir.write("game/wind_project.toml", "name = \"Game\"\nengine = \"0.1.0\"\ntarget = \"game\"\n");
    const launcher::ProjectEntry good = launcher::read_project_entry(dir.path() / "game");
    ASSERT_TRUE(good.project.has_value());
    EXPECT_EQ(good.project->name, "Game");
    EXPECT_TRUE(good.problem.empty());

    const launcher::ProjectEntry gone = launcher::read_project_entry(dir.path() / "moved");
    EXPECT_FALSE(gone.project.has_value());
    EXPECT_NE(gone.problem.find("File not found"), std::string::npos);
}
