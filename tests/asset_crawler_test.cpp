#include <gtest/gtest.h>

#include <engine/resources/asset_id.h>
#include <engine/resources/meta.h>

#include "resources/asset_crawler.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

class TempAssetTree {
public:
    TempAssetTree() {
        path_ = std::filesystem::temp_directory_path() / ("wind_crawler_test_" + std::to_string(counter_++));
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TempAssetTree() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    void write(const std::string& rel_path, const std::string& content, const std::string& guid, const std::string& importer = "data") {
        const std::filesystem::path full = path_ / rel_path;
        std::filesystem::create_directories(full.parent_path());
        std::ofstream f(full, std::ios::binary);
        f << content;

        std::ofstream m(full.string() + ".meta", std::ios::binary);
        m << "guid = \"" << guid << "\"\nimporter = \"" << importer << "\"\n";
    }

    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

private:
    static inline int counter_ = 0;
    std::filesystem::path path_;
};

} // namespace

TEST(AssetCrawler, ExtractAssetDependencies) {
    const std::string content =
            "some text\n"
            "shader = \"asset://0123456789abcdef0123456789abcdef\"\n"
            "unrelated = \"0123456789abcdef0123456789abcdef\"\n"
            "another = \"asset://fedcba9876543210fedcba9876543210\"\n"
            "bad = \"asset://not-a-valid-guid-at-all-here\"\n";

    const auto deps = engine::extract_asset_dependencies(content);
    ASSERT_EQ(deps.size(), 2u);
    EXPECT_EQ(deps[0].hex(), "0123456789abcdef0123456789abcdef");
    EXPECT_EQ(deps[1].hex(), "fedcba9876543210fedcba9876543210");
}

TEST(AssetCrawler, GlobMatch) {
    EXPECT_TRUE(engine::glob_match("audio/sfx/*", "audio/sfx/jump.wav"));
    EXPECT_TRUE(engine::glob_match("audio/**", "audio/music/boss/theme.ogg"));
    EXPECT_TRUE(engine::glob_match("*.png", "hero.png"));
    EXPECT_FALSE(engine::glob_match("*.png", "hero.wav"));
    EXPECT_TRUE(engine::glob_match("strings/*", "strings/en.strings"));
    EXPECT_FALSE(engine::glob_match("audio/sfx/*", "textures/hero.png"));
}

TEST(AssetCrawler, ReachabilityTreeShaking) {
    TempAssetTree tree;
    constexpr const char* kRootWorldGuid = "11111111111111111111111111111111";
    constexpr const char* kObjectGuid    = "22222222222222222222222222222222";
    constexpr const char* kMatGuid       = "33333333333333333333333333333333";
    constexpr const char* kShaderGuid    = "44444444444444444444444444444444";
    constexpr const char* kTextureGuid   = "55555555555555555555555555555555";
    constexpr const char* kSfxGuid       = "66666666666666666666666666666666";
    constexpr const char* kUnusedGuid    = "77777777777777777777777777777777";

    // Root world references object
    tree.write("scenes/main.world",
            "version = 1\n[[entities]]\nid = \"1\"\n[entities.components.custom]\nobj = \"asset://" + std::string(kObjectGuid) + "\"\n",
            kRootWorldGuid, "world");

    // Object references material
    tree.write("data/hero.object",
            "type = \"HeroData\"\nmat = \"asset://" + std::string(kMatGuid) + "\"\n",
            kObjectGuid, "object");

    // Material references shader and texture
    tree.write("materials/hero.mat",
            "shader = \"asset://" + std::string(kShaderGuid) + "\"\n[textures]\nalbedo = \"asset://" + std::string(kTextureGuid) + "\"\n",
            kMatGuid, "material");

    // Shader and texture
    tree.write("shaders/hero.shader",
            "<shader><glsl><vertex><![CDATA[void main(){}]]></vertex><fragment><![CDATA[void main(){}]]></fragment></glsl></shader>",
            kShaderGuid, "shader");
    tree.write("textures/hero.png", "fake_png_data", kTextureGuid, "texture");

    // Dynamic sound included via always_include
    tree.write("audio/sfx/boom.wav", "fake_wav", kSfxGuid, "audio");

    // Unused asset (not in roots, not in always_include, not referenced)
    tree.write("textures/unused.png", "unused_data", kUnusedGuid, "texture");

    std::vector<std::string> roots{"scenes/main.world"};
    std::vector<std::string> always_include{"audio/sfx/*"};

    auto scanned = engine::codegen_scan(tree.path(), {}, roots, always_include);
    ASSERT_TRUE(scanned.has_value()) << (scanned ? "" : scanned.error().message);

    // Verify catalog has reachable assets
    const auto& entries = scanned->catalog.entries();
    std::unordered_set<std::string> catalog_guids;
    for (const auto& e : entries) {
        catalog_guids.insert(std::string(e.guid.hex()));
    }

    EXPECT_TRUE(catalog_guids.contains(kRootWorldGuid));
    EXPECT_TRUE(catalog_guids.contains(kObjectGuid));
    EXPECT_TRUE(catalog_guids.contains(kMatGuid));
    EXPECT_TRUE(catalog_guids.contains(kShaderGuid));
    EXPECT_TRUE(catalog_guids.contains(kTextureGuid));
    EXPECT_TRUE(catalog_guids.contains(kSfxGuid));
    EXPECT_FALSE(catalog_guids.contains(kUnusedGuid)); // Tree-shaken!

    // Verify asset_ids.h has ONLY reachable IDs
    EXPECT_NE(scanned->asset_ids_header.find("main"), std::string::npos);
    EXPECT_NE(scanned->asset_ids_header.find("hero"), std::string::npos);
    EXPECT_NE(scanned->asset_ids_header.find("boom"), std::string::npos);
    EXPECT_EQ(scanned->asset_ids_header.find("unused"), std::string::npos); // Not emitted!
}

TEST(AssetCrawler, StageOnlyCopiesReachableAssetsAndNoMetas) {
    TempAssetTree tree;
    constexpr const char* kRootGuid = "11111111111111111111111111111111";
    constexpr const char* kUnusedGuid = "22222222222222222222222222222222";

    tree.write("main.txt", "hello", kRootGuid, "data");
    tree.write("unused.txt", "bye", kUnusedGuid, "data");

    const std::filesystem::path out_gen = tree.path() / "gen";
    const std::filesystem::path out_stage = tree.path() / "staged";

    std::vector<std::string> roots{"main.txt"};
    const auto result = engine::codegen_write(tree.path(), out_gen, {}, roots, {}, out_stage);
    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error().message);

    EXPECT_TRUE(std::filesystem::exists(out_stage / "catalog.toml"));
    EXPECT_TRUE(std::filesystem::exists(out_stage / "main.txt"));
    EXPECT_FALSE(std::filesystem::exists(out_stage / "main.txt.meta")); // No .meta!
    EXPECT_FALSE(std::filesystem::exists(out_stage / "unused.txt"));    // Tree shaken!
    EXPECT_FALSE(std::filesystem::exists(out_stage / "unused.txt.meta"));
}
