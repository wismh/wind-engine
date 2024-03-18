#include <gtest/gtest.h>

#include <engine/core/platform.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <thread>

namespace {

std::filesystem::path unique_temp(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

// On Windows, a file just written can still be held by a antivirus/indexer scan for a few
// milliseconds; remove_all() then fails with ERROR_SHARING_VIOLATION even though nothing in
// this process holds it open. Retry with backoff instead of letting that flake the test.
void remove_all_retry(const std::filesystem::path& path) {
    std::error_code ec;
    for (int attempt = 0; attempt < 6; ++attempt) {
        std::filesystem::remove_all(path, ec);
        if (!ec) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10 << attempt));
    }
}

}

TEST(AndroidAssets, ApkMountIsAssetsScheme) {
    EXPECT_EQ(engine::apk_assets_mount(), "assets://");
}

TEST(AndroidAssets, EmptyBaseYieldsEmptyFilesystemRoot) {
    EXPECT_TRUE(engine::default_assets_root({}, engine::Platform::Android).empty());
}

TEST(AndroidAssets, NonEmptyBaseIsFilesystemTree) {
    const std::filesystem::path root = engine::default_assets_root(std::filesystem::path{"/data/user/0/app"},
            engine::Platform::Android);
    EXPECT_EQ(root, std::filesystem::path{"/data/user/0/app"} / "assets");
}

TEST(AndroidAssets, StageCopiesCatalogAndFiles) {
    const auto root = unique_temp("wind-android-assets-stage");
    const auto src = root / "src";
    const auto dest = root / "dest";
    remove_all_retry(root);
    std::filesystem::create_directories(src / "engine");
    {
        std::ofstream{src / "catalog.toml"} << "guid = \"game\"\n";
        std::ofstream{src / "engine" / "catalog.toml"} << "guid = \"engine\"\n";
        std::ofstream{src / "sprite.png"} << "png";
    }

    ASSERT_TRUE(engine::stage_android_assets(src, dest));
    EXPECT_TRUE(std::filesystem::is_regular_file(dest / "catalog.toml"));
    EXPECT_TRUE(std::filesystem::is_regular_file(dest / "engine" / "catalog.toml"));
    EXPECT_TRUE(std::filesystem::is_regular_file(dest / "sprite.png"));

    std::ifstream in(dest / "catalog.toml");
    const std::string body{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    EXPECT_NE(body.find("game"), std::string::npos);

    remove_all_retry(root);
}

TEST(AndroidAssets, StageRejectsMissingSource) {
    EXPECT_FALSE(engine::stage_android_assets({}, std::filesystem::path{"/tmp/wind-android-dest"}));
    EXPECT_FALSE(engine::stage_android_assets(std::filesystem::path{"/no/such/wind-android-src"},
            unique_temp("wind-android-assets-missing-dest")));
}

TEST(AndroidAssets, NativeAndWebRootsUnchanged) {
    EXPECT_EQ(engine::default_assets_root(std::filesystem::path{"/tmp/game"}, engine::Platform::Native),
            std::filesystem::path{"/tmp/game"} / "assets");
    EXPECT_EQ(engine::default_assets_root(std::filesystem::path{"/tmp/game"}, engine::Platform::Web),
            engine::packaged_assets_mount());
}
