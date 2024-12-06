#include <gtest/gtest.h>

#include <engine/core/platform.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::filesystem::path unique_temp(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

}

TEST(AndroidAssets, ApkMountIsAssetsScheme) {
    EXPECT_EQ(engine::apk_assets_mount().generic_string(), "assets://");
}

TEST(AndroidAssets, EmptyBaseUsesApkMount) {
    EXPECT_EQ(engine::default_assets_root({}, engine::Platform::Android), engine::apk_assets_mount());
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
    std::filesystem::remove_all(root);
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

    std::filesystem::remove_all(root);
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
