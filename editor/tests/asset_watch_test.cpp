#include <gtest/gtest.h>

#include "asset_sync.h"
#include "asset_watch.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

class Tree {
public:
    Tree()
        : root_(std::filesystem::temp_directory_path() / "wind_asset_watch_test" /
                ::testing::UnitTest::GetInstance()->current_test_info()->name()) {
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_ / "assets");
    }
    ~Tree() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }
    Tree(const Tree&) = delete;
    Tree& operator=(const Tree&) = delete;

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

    void write(const std::string& relative, const std::string& text) const {
        const std::filesystem::path path = root_ / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    }

    [[nodiscard]] std::string read(const std::string& relative) const {
        std::ifstream in(root_ / relative, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), {}};
    }

private:
    std::filesystem::path root_;
};

std::filesystem::path assets(const Tree& tree, const std::string& relative) {
    return tree.root() / "assets" / relative;
}

} // namespace

TEST(AssetSync, NewFileGetsAMetaAndTheNextSyncWritesNothing) {
    Tree tree;
    tree.write("assets/sprites/hero.png", "png");
    tree.write("assets/loot.json", "{}");

    const editor::AssetSyncCounts first = editor::sync_assets(tree.root() / "assets");
    EXPECT_EQ(first.written, 2);
    EXPECT_EQ(first.removed, 0);
    const std::string png_meta = tree.read("assets/sprites/hero.png.meta");
    const std::string json_meta = tree.read("assets/loot.json.meta");
    EXPECT_NE(png_meta.find("importer = \"texture\""), std::string::npos);
    EXPECT_NE(json_meta.find("importer = \"data\""), std::string::npos);
    EXPECT_NE(png_meta.find("guid = \""), std::string::npos);

    const editor::AssetSyncCounts second = editor::sync_assets(tree.root() / "assets");
    EXPECT_EQ(second.written, 0);
    EXPECT_EQ(second.removed, 0);
    EXPECT_EQ(tree.read("assets/sprites/hero.png.meta"), png_meta);
}

TEST(AssetSync, ExistingGuidIsKeptAndSkippedNamesGetNoMeta) {
    Tree tree;
    const std::string meta = "guid = \"0123456789abcdef0123456789abcdef\"\nimporter = \"texture\"\n";
    tree.write("assets/sprites/hero.png", "png");
    tree.write("assets/sprites/hero.png.meta", meta);
    tree.write("assets/readme.md", "hi");
    tree.write("assets/notes.md", "hi");
    tree.write("assets/catalog.toml", "");
    tree.write("assets/.secret.png", "png");

    const editor::AssetSyncCounts counts = editor::sync_assets(tree.root() / "assets");
    EXPECT_EQ(counts.written, 0);
    EXPECT_EQ(counts.removed, 0);
    EXPECT_EQ(tree.read("assets/sprites/hero.png.meta"), meta);
    EXPECT_FALSE(std::filesystem::exists(assets(tree, "readme.md.meta")));
    EXPECT_FALSE(std::filesystem::exists(assets(tree, "notes.md.meta")));
    EXPECT_FALSE(std::filesystem::exists(assets(tree, "catalog.toml.meta")));
    EXPECT_FALSE(std::filesystem::exists(assets(tree, ".secret.png.meta")));
}

TEST(AssetSync, OrphanMetaIsRemovedAndAPairIsKept) {
    Tree tree;
    const std::string meta = "guid = \"0123456789abcdef0123456789abcdef\"\nimporter = \"texture\"\n";
    tree.write("assets/sprites/hero.png", "png");
    tree.write("assets/sprites/hero.png.meta", meta);
    tree.write("assets/old/gone.png.meta", "guid = \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"\nimporter = \"texture\"\n");
    tree.write("assets/sprites.meta", "guid = \"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"\nimporter = \"data\"\n");

    const editor::AssetSyncCounts counts = editor::sync_assets(tree.root() / "assets");
    EXPECT_EQ(counts.written, 0);
    EXPECT_EQ(counts.removed, 1);
    EXPECT_FALSE(std::filesystem::exists(assets(tree, "old/gone.png.meta")));
    EXPECT_EQ(tree.read("assets/sprites/hero.png.meta"), meta);
    EXPECT_TRUE(std::filesystem::exists(assets(tree, "sprites.meta")));
}

TEST(AssetSync, MissingDirectoryDoesNothing) {
    Tree tree;
    const editor::AssetSyncCounts counts = editor::sync_assets(tree.root() / "no-such-assets");
    EXPECT_EQ(counts.written, 0);
    EXPECT_EQ(counts.removed, 0);
}

TEST(AssetWatch, OpenSyncsBeforeAnyPoll) {
    Tree tree;
    tree.write("assets/sprites/hero.png", "png");
    editor::AssetWatch watch;
    watch.open(tree.root());
    EXPECT_TRUE(std::filesystem::exists(assets(tree, "sprites/hero.png.meta")));
}

TEST(AssetWatch, ChangeAppliesOnceTheFingerprintSettles) {
    Tree tree;
    tree.write("assets/sprites/hero.png", "png");
    editor::AssetWatch watch;
    watch.open(tree.root());

    tree.write("assets/sprites/extra.png", "x");
    watch.notice();
    const auto t0 = std::chrono::steady_clock::time_point{};
    EXPECT_FALSE(watch.poll(t0));
    EXPECT_FALSE(std::filesystem::exists(assets(tree, "sprites/extra.png.meta")));
    EXPECT_FALSE(watch.poll(t0));
    EXPECT_TRUE(watch.poll(t0 + editor::AssetWatch::kSampleInterval));
    EXPECT_TRUE(std::filesystem::exists(assets(tree, "sprites/extra.png.meta")));
}

TEST(AssetWatch, SidecarWrittenBySyncDoesNotSignalAgain) {
    Tree tree;
    tree.write("assets/sprites/hero.png", "png");
    editor::AssetWatch watch;
    watch.open(tree.root());

    tree.write("assets/sprites/extra.png", "x");
    watch.notice();
    const auto t0 = std::chrono::steady_clock::time_point{};
    EXPECT_FALSE(watch.poll(t0));
    EXPECT_TRUE(watch.poll(t0 + editor::AssetWatch::kSampleInterval));
    const std::string meta = tree.read("assets/sprites/extra.png.meta");
    watch.notice();
    EXPECT_FALSE(watch.poll(t0 + 2 * editor::AssetWatch::kSampleInterval));
    EXPECT_FALSE(watch.poll(t0 + 3 * editor::AssetWatch::kSampleInterval));
    EXPECT_EQ(tree.read("assets/sprites/extra.png.meta"), meta);
}

TEST(AssetWatch, SizeChangeRescansWithoutRewritingTheMeta) {
    Tree tree;
    tree.write("assets/sprites/hero.png", "png");
    editor::AssetWatch watch;
    watch.open(tree.root());
    const std::string meta = tree.read("assets/sprites/hero.png.meta");

    tree.write("assets/sprites/hero.png", "png-bigger");
    watch.notice();
    const auto t0 = std::chrono::steady_clock::time_point{};
    EXPECT_FALSE(watch.poll(t0));
    EXPECT_TRUE(watch.poll(t0 + editor::AssetWatch::kSampleInterval));
    EXPECT_EQ(tree.read("assets/sprites/hero.png.meta"), meta);
}

TEST(AssetWatch, DeletingAFileRemovesItsMetaWhenSettled) {
    Tree tree;
    tree.write("assets/sprites/hero.png", "png");
    editor::AssetWatch watch;
    watch.open(tree.root());
    std::filesystem::remove(assets(tree, "sprites/hero.png"));
    watch.notice();
    const auto t0 = std::chrono::steady_clock::time_point{};
    EXPECT_FALSE(watch.poll(t0));
    EXPECT_TRUE(std::filesystem::exists(assets(tree, "sprites/hero.png.meta")));
    EXPECT_TRUE(watch.poll(t0 + editor::AssetWatch::kSampleInterval));
    EXPECT_FALSE(std::filesystem::exists(assets(tree, "sprites/hero.png.meta")));
}
