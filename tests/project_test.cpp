#include <gtest/gtest.h>

#include <engine/project/manifest_error.h>
#include <engine/project/sdk_manifest.h>
#include <engine/project/wind_project.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

// A fresh directory under the temp directory, removed with the object.
class TempDir {
public:
    explicit TempDir(std::string_view name)
        : path_(std::filesystem::temp_directory_path() / "wind_project_test" / name) {
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

    void write(std::string_view file, std::string_view text) const {
        std::ofstream(path_ / file, std::ios::binary) << text;
    }

private:
    std::filesystem::path path_;
};

constexpr char kSdk[] = R"(# Wind editor SDK. Written by cmake --install. Do not edit.
version = "0.1.0"
commit = "50c292f95fec1d06617eeb17409cee2b612691e2"
dirty = false
config = "Release"
build_id = "5266d5a5ebe5603e"
)";

}

TEST(WindProject, ReadsTheKeys) {
    const TempDir dir{"tic_tac_toe"};
    dir.write(engine::kWindProjectFile, "name = \"Tic Tac Toe\"\nengine = \"0.1.0\"\ntarget = \"tic-tac-toe\"\nextra = 3\n");
    const auto project = engine::read_wind_project(dir.path());
    ASSERT_TRUE(project.has_value()) << engine::describe(project.error());
    EXPECT_EQ(project->name, "Tic Tac Toe");
    EXPECT_EQ(project->engine, "0.1.0");
    EXPECT_EQ(project->target, "tic-tac-toe");
}

TEST(WindProject, NameDefaultsToTheDirectory) {
    const TempDir dir{"my_game"};
    dir.write(engine::kWindProjectFile, "engine = \"0.1.0\"\ntarget = \"my_game\"\n");
    const auto project = engine::read_wind_project(dir.path());
    ASSERT_TRUE(project.has_value());
    EXPECT_EQ(project->name, "my_game");
    const auto slashed = engine::read_wind_project(dir.path() / "");
    ASSERT_TRUE(slashed.has_value());
    EXPECT_EQ(slashed->name, "my_game");
}

TEST(WindProject, ReportsWhatIsWrong) {
    const TempDir dir{"broken"};
    const auto missing = engine::read_wind_project(dir.path());
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().kind, engine::ManifestError::Missing);

    dir.write(engine::kWindProjectFile, "engine = \"0.1.0\"\n");
    const auto no_target = engine::read_wind_project(dir.path());
    ASSERT_FALSE(no_target.has_value());
    EXPECT_EQ(no_target.error().kind, engine::ManifestError::MissingKey);
    EXPECT_EQ(engine::describe(no_target.error()), "Missing key: wind_project.toml: target");

    dir.write(engine::kWindProjectFile, "engine = 1\ntarget = \"x\"\n");
    const auto number = engine::read_wind_project(dir.path());
    ASSERT_FALSE(number.has_value());
    EXPECT_EQ(number.error().kind, engine::ManifestError::WrongType);

    dir.write(engine::kWindProjectFile, "engine = \"0.1.0\ntarget = \"x\"\n");
    const auto malformed = engine::read_wind_project(dir.path());
    ASSERT_FALSE(malformed.has_value());
    EXPECT_EQ(malformed.error().kind, engine::ManifestError::Malformed);
    EXPECT_NE(malformed.error().detail.find(":1:"), std::string::npos) << malformed.error().detail;
}

TEST(SdkManifest, ReadsWhatTheInstallWrites) {
    const TempDir dir{"sdk"};
    dir.write(engine::kSdkManifestFile, kSdk);
    const auto sdk = engine::read_sdk_manifest(dir.path());
    ASSERT_TRUE(sdk.has_value()) << engine::describe(sdk.error());
    EXPECT_EQ(sdk->version, "0.1.0");
    EXPECT_EQ(sdk->commit, "50c292f95fec1d06617eeb17409cee2b612691e2");
    EXPECT_FALSE(sdk->dirty);
    EXPECT_EQ(sdk->config, "Release");
    EXPECT_EQ(sdk->build_id, "5266d5a5ebe5603e");
}

TEST(SdkManifest, IgnoresNewKeysAndNeedsEveryKnownOne) {
    const TempDir dir{"sdk_keys"};
    dir.write(engine::kSdkManifestFile, std::string(kSdk) + "platform = \"windows-x64\"\n");
    EXPECT_TRUE(engine::read_sdk_manifest(dir.path()).has_value());

    dir.write(engine::kSdkManifestFile, "version = \"0.1.0\"\ncommit = \"a\"\nconfig = \"Release\"\nbuild_id = \"b\"\n");
    const auto no_dirty = engine::read_sdk_manifest(dir.path());
    ASSERT_FALSE(no_dirty.has_value());
    EXPECT_EQ(no_dirty.error().kind, engine::ManifestError::MissingKey);
    EXPECT_NE(no_dirty.error().detail.find("dirty"), std::string::npos);

    dir.write(engine::kSdkManifestFile, "version = \"0.1.0\"\ncommit = \"a\"\ndirty = \"no\"\nconfig = \"R\"\nbuild_id = \"b\"\n");
    const auto text_dirty = engine::read_sdk_manifest(dir.path());
    ASSERT_FALSE(text_dirty.has_value());
    EXPECT_EQ(text_dirty.error().kind, engine::ManifestError::WrongType);
}
