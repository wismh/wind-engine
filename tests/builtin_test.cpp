#include <gtest/gtest.h>

#include <engine/builtin_ids.h>
#include <engine/render/material.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/meta.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#ifndef ENGINE_BUILTIN_ASSETS_DIR
#error "ENGINE_BUILTIN_ASSETS_DIR must be set to the builtin_assets path"
#endif

namespace {

constexpr std::string_view kShaderUnlit = "a0e1b2c3d4f5678901234567890abc01";
constexpr std::string_view kMeshQuad = "a0e1b2c3d4f5678901234567890abc02";
constexpr std::string_view kMaterialUnlit = "a0e1b2c3d4f5678901234567890abc03";
constexpr std::string_view kFontUi = "a0e1b2c3d4f5678901234567890abc04";
constexpr std::string_view kSplashWind = "a0e1b2c3d4f5678901234567890abc05";

std::filesystem::path builtin_assets_dir() {
    return std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR};
}

struct TempTree {
    std::filesystem::path path;

    TempTree() {
        static int seq = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
                ("wind_builtin_" + std::to_string(stamp) + "_" + std::to_string(++seq));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    ~TempTree() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    TempTree(const TempTree&) = delete;
    TempTree& operator=(const TempTree&) = delete;
};

void write_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << text;
}

}

TEST(Builtin, BuiltinIdsAreFrozen) {
    EXPECT_EQ(engine::builtin::shader_unlit.hex(), kShaderUnlit);
    EXPECT_EQ(engine::builtin::mesh_quad.hex(), kMeshQuad);
    EXPECT_EQ(engine::builtin::material_unlit.hex(), kMaterialUnlit);
    EXPECT_EQ(engine::builtin::font_ui.hex(), kFontUi);
    EXPECT_EQ(engine::builtin::splash_wind.hex(), kSplashWind);

    EXPECT_TRUE(engine::AssetId::is_valid(engine::builtin::shader_unlit.hex()));
    EXPECT_TRUE(engine::AssetId::is_valid(engine::builtin::mesh_quad.hex()));
    EXPECT_TRUE(engine::AssetId::is_valid(engine::builtin::material_unlit.hex()));
    EXPECT_TRUE(engine::AssetId::is_valid(engine::builtin::font_ui.hex()));
    EXPECT_TRUE(engine::AssetId::is_valid(engine::builtin::splash_wind.hex()));
}

TEST(Builtin, BuiltinIdsAreUnique) {
    EXPECT_NE(engine::builtin::shader_unlit, engine::builtin::mesh_quad);
    EXPECT_NE(engine::builtin::shader_unlit, engine::builtin::material_unlit);
    EXPECT_NE(engine::builtin::shader_unlit, engine::builtin::font_ui);
    EXPECT_NE(engine::builtin::shader_unlit, engine::builtin::splash_wind);
    EXPECT_NE(engine::builtin::mesh_quad, engine::builtin::material_unlit);
    EXPECT_NE(engine::builtin::mesh_quad, engine::builtin::font_ui);
    EXPECT_NE(engine::builtin::mesh_quad, engine::builtin::splash_wind);
    EXPECT_NE(engine::builtin::material_unlit, engine::builtin::font_ui);
    EXPECT_NE(engine::builtin::material_unlit, engine::builtin::splash_wind);
    EXPECT_NE(engine::builtin::font_ui, engine::builtin::splash_wind);
    EXPECT_EQ(engine::builtin::count(), 5u);
    EXPECT_EQ(engine::builtin::reserved().size(), 5u);
}

TEST(Builtin, BuiltinMetasMatchIds) {
    const std::filesystem::path root = builtin_assets_dir();
    ASSERT_TRUE(std::filesystem::is_directory(root));

    std::unordered_map<std::string, engine::ImporterKind> by_guid;
    std::error_code iter_ec;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, options, iter_ec)) {
        ASSERT_FALSE(iter_ec);
        if (!entry.is_regular_file() || entry.path().extension() != ".meta") {
            continue;
        }
        std::ifstream in(entry.path());
        ASSERT_TRUE(in.is_open()) << entry.path().generic_string();
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto parsed = engine::parse_asset_meta(text);
        ASSERT_TRUE(parsed.has_value()) << entry.path().generic_string();
        by_guid.emplace(std::string(parsed->guid.hex()), parsed->importer);
    }

    const auto require_importer = [&](std::string_view guid, engine::ImporterKind kind) {
        const auto it = by_guid.find(std::string(guid));
        ASSERT_TRUE(it != by_guid.end()) << guid;
        EXPECT_EQ(it->second, kind);
    };
    require_importer(kShaderUnlit, engine::ImporterKind::Shader);
    require_importer(kMeshQuad, engine::ImporterKind::Mesh);
    require_importer(kMaterialUnlit, engine::ImporterKind::Material);
    require_importer(kFontUi, engine::ImporterKind::Font);
    require_importer(kSplashWind, engine::ImporterKind::UiImage);
}

TEST(Builtin, BuiltinMaterialReferencesShader) {
    const std::filesystem::path mat = builtin_assets_dir() / "materials" / "unlit.mat";
    const auto desc = engine::render::parse_material_file(mat.string());
    ASSERT_TRUE(desc.has_value());
    EXPECT_EQ(desc->shader, kShaderUnlit);
    EXPECT_EQ(desc->blend, engine::render::BlendMode::Alpha);
}

TEST(Builtin, CodegenRejectsBuiltinGuidReuse) {
    TempTree tree;
    write_file(tree.path / "stolen.shader", "not-a-shader");
    write_file(tree.path / "stolen.shader.meta",
            std::string("guid = \"") + std::string(kShaderUnlit) + "\"\nimporter = \"shader\"\n");

    const auto result = engine::codegen_scan(tree.path, engine::builtin::reserved());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind, engine::CodegenErrorKind::Collision);
}

TEST(Builtin, CodegenScanBuiltinAssets) {
    const auto result = engine::codegen_scan(builtin_assets_dir());
    ASSERT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    EXPECT_GE(result->catalog.entries().size(), 5u);
    EXPECT_EQ(result->asset_ids_header.find("binding_id.h"), std::string::npos);
}
