#include <gtest/gtest.h>

#include <engine/builtin_ids.h>
#include <engine/ecs/model.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_serializer.h>
#include <engine/render/graphic_factory.h>
#include <engine/render/model.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/meta.h>

#include "fixtures/fatal_fakes.h"
#include "gltf/cook.h"
#include "render/mesh_check.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef ENGINE_BUILTIN_ASSETS_DIR
#error "ENGINE_BUILTIN_ASSETS_DIR must be set"
#endif

namespace {

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 24));
}

void append_f32(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    append_u32(out, bits);
}

std::vector<std::uint8_t> glb(std::string json, std::vector<std::uint8_t> bin) {
    while (json.size() % 4 != 0) {
        json.push_back(' ');
    }
    while (bin.size() % 4 != 0) {
        bin.push_back(0);
    }
    const std::uint32_t json_length = static_cast<std::uint32_t>(json.size());
    const std::uint32_t bin_length = static_cast<std::uint32_t>(bin.size());
    const std::uint32_t total = 12 + 8 + json_length + (bin.empty() ? 0 : 8 + bin_length);
    std::vector<std::uint8_t> out;
    append_u32(out, 0x46546C67u);
    append_u32(out, 2);
    append_u32(out, total);
    append_u32(out, json_length);
    append_u32(out, 0x4E4F534Au);
    out.insert(out.end(), json.begin(), json.end());
    if (!bin.empty()) {
        append_u32(out, bin_length);
        append_u32(out, 0x004E4942u);
        out.insert(out.end(), bin.begin(), bin.end());
    }
    return out;
}

std::string_view bytes_of(const std::vector<std::uint8_t>& bytes) {
    return std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

engine::gltf::CookError cook_error(const std::vector<std::uint8_t>& file) {
    auto cooked = engine::gltf::cook_gltf(bytes_of(file), {});
    EXPECT_FALSE(cooked.has_value());
    if (cooked) {
        return {};
    }
    return cooked.error();
}

class TestMesh : public engine::render::IMesh {
public:
    explicit TestMesh(const engine::render::MeshDesc& desc)
        : engine::render::IMesh(desc) {
    }

protected:
    bool replace(const engine::render::MeshDesc&) override {
        return true;
    }
};

class TestShader : public engine::render::IShader {};

class TestTexture : public engine::render::ITexture {};

class TestDynamicTexture : public engine::render::IDynamicTexture {
public:
    bool update(std::span<const std::uint8_t>, int, int) override {
        return true;
    }
    bool update_rects(std::span<const engine::render::TextureRect>, std::span<const std::uint8_t>, int) override {
        return true;
    }
};

class TestFactory : public engine::render::IGraphicFactory {
public:
    std::shared_ptr<engine::render::IShader> create_shader(const engine::render::ShaderDesc&) override {
        return std::make_shared<TestShader>();
    }
    std::shared_ptr<engine::render::ITexture> create_texture(const engine::render::TextureDesc&) override {
        return std::make_shared<TestTexture>();
    }
    std::shared_ptr<engine::render::ITexture> create_texture_array(const engine::render::TextureArrayDesc&) override {
        return std::make_shared<TestTexture>();
    }
    std::shared_ptr<engine::render::IDynamicTexture> create_dynamic_texture(const engine::render::DynamicTextureDesc&) override {
        return std::make_shared<TestDynamicTexture>();
    }
    std::shared_ptr<engine::render::IMesh> create_mesh(const engine::render::MeshDesc& desc) override {
        if (!engine::render::mesh_desc_error(desc).empty()) {
            return {};
        }
        return std::make_shared<TestMesh>(desc);
    }
};

}

TEST(Model, StaticTriangleMatchesMesh) {
    std::vector<std::uint8_t> bin;
    const float positions[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    for (float value : positions) {
        append_f32(bin, value);
    }
    const char* json =
            R"({"asset":{"version":"2.0"},"nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
            R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],)"
            R"("bufferViews":[{"buffer":0,"byteLength":36}],"buffers":[{"byteLength":36}]})";
    auto cooked = engine::gltf::cook_gltf(bytes_of(glb(json, bin)), {});
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
    auto model = engine::render::parse_model_blob(*cooked);
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->primitives.size(), 1u);
    ASSERT_EQ(model->primitives[0].mesh.vertices.size(), 3u);
    EXPECT_TRUE(model->primitives[0].mesh.influences.empty());
    EXPECT_EQ(model->primitives[0].mesh.vertices[1].position, glm::vec3(1, 0, 0));
}

TEST(Model, TwoBonePoseAndBounds) {
    std::vector<std::uint8_t> bin;
    const float positions[] = {1, 1, 0, 0, 0, 0, 1, 0, 0};
    for (float value : positions) {
        append_f32(bin, value);
    }
    const std::uint8_t joints[] = {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    bin.insert(bin.end(), joints, joints + sizeof(joints));
    const float weights[] = {0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    for (float value : weights) {
        append_f32(bin, value);
    }
    const float identity[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float bind_b[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1};
    for (float value : identity) {
        append_f32(bin, value);
    }
    for (float value : bind_b) {
        append_f32(bin, value);
    }
    append_f32(bin, 0);
    append_f32(bin, 1);
    const float half = std::sin(glm::radians(45.0f));
    const float quats[] = {0, 0, 0, 1, 0, 0, half, half};
    for (float value : quats) {
        append_f32(bin, value);
    }
    ASSERT_EQ(bin.size(), 264u);

    const char* json =
            R"({"asset":{"version":"2.0"},"nodes":[{"mesh":0,"skin":0},{"name":"a"},{"name":"b","translation":[0,1,0]}],)"
            R"("skins":[{"joints":[1,2],"inverseBindMatrices":3}],)"
            R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2}}]}],)"
            R"("accessors":[)"
            R"({"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)"
            R"({"bufferView":1,"componentType":5121,"count":3,"type":"VEC4"},)"
            R"({"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},)"
            R"({"bufferView":3,"componentType":5126,"count":2,"type":"MAT4"},)"
            R"({"bufferView":4,"componentType":5126,"count":2,"type":"SCALAR"},)"
            R"({"bufferView":5,"componentType":5126,"count":2,"type":"VEC4"}])"
            R"(,"bufferViews":[)"
            R"({"buffer":0,"byteOffset":0,"byteLength":36},)"
            R"({"buffer":0,"byteOffset":36,"byteLength":12},)"
            R"({"buffer":0,"byteOffset":48,"byteLength":48},)"
            R"({"buffer":0,"byteOffset":96,"byteLength":128},)"
            R"({"buffer":0,"byteOffset":224,"byteLength":8},)"
            R"({"buffer":0,"byteOffset":232,"byteLength":32}],)"
            R"("buffers":[{"byteLength":264}],)"
            R"("animations":[{"name":"spin","channels":[{"sampler":0,"target":{"node":2,"path":"rotation"}}],)"
            R"("samplers":[{"input":4,"output":5,"interpolation":"LINEAR"}]}]})";

    auto cooked = engine::gltf::cook_gltf(bytes_of(glb(json, std::move(bin))), {});
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
    auto model = engine::render::parse_model_blob(*cooked);
    ASSERT_TRUE(model.has_value());
    const auto locals = engine::render::sample_model_locals(*model, "spin", 1.0f, false);
    const auto global = engine::render::model_globals(*model, locals);
    const auto palette = engine::render::model_skin_palette(*model, 0, 0, global);
    ASSERT_EQ(palette.size(), 2u);
    const glm::mat4 expected = glm::translate(glm::mat4{1}, glm::vec3(0, 1, 0)) *
                               glm::mat4_cast(glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1))) *
                               glm::translate(glm::mat4{1}, glm::vec3(0, -1, 0));
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_NEAR(palette[1][column][row], expected[column][row], 1e-4f) << column << " " << row;
        }
    }
    const glm::vec3 posed = glm::vec3(palette[1] * glm::vec4(1, 1, 0, 1));
    EXPECT_NEAR(posed.x, 0.0f, 1e-4f);
    EXPECT_NEAR(posed.y, 2.0f, 1e-4f);
    ASSERT_TRUE(model->primitives[0].mesh.bounds_override.has_value());
    const engine::render::Aabb& box = *model->primitives[0].mesh.bounds_override;
    EXPECT_LE(box.min.x, 0.0f);
    EXPECT_GE(box.max.x, 1.0f);
    EXPECT_LE(box.min.y, 1.0f);
    EXPECT_GE(box.max.y, 2.0f);

    engine::render::MeshDesc desc = model->primitives[0].mesh;
    TestMesh mesh(desc);
    EXPECT_LE(mesh.bounds().min.y, 1.0f);
    EXPECT_GE(mesh.bounds().max.y, 2.0f);
}

TEST(Model, ExtraWeightsAreCutAndNormalized) {
    std::vector<std::uint8_t> bin;
    for (int vertex = 0; vertex < 3; ++vertex) {
        append_f32(bin, 0);
        append_f32(bin, 0);
        append_f32(bin, 0);
    }
    for (int vertex = 0; vertex < 3; ++vertex) {
        for (std::uint8_t joint : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{3}}) {
            bin.push_back(joint);
        }
    }
    const float first[] = {0.40f, 0.25f, 0.15f, 0.10f};
    for (int vertex = 0; vertex < 3; ++vertex) {
        for (float weight : first) {
            append_f32(bin, weight);
        }
    }
    for (int vertex = 0; vertex < 3; ++vertex) {
        for (std::uint8_t joint : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{3}}) {
            bin.push_back(joint);
        }
    }
    const float second[] = {0.04f, 0.03f, 0.02f, 0.01f};
    for (int vertex = 0; vertex < 3; ++vertex) {
        for (float weight : second) {
            append_f32(bin, weight);
        }
    }
    ASSERT_EQ(bin.size(), 156u);
    const char* json =
            R"({"asset":{"version":"2.0"},"nodes":[{"mesh":0,"skin":0},{},{},{},{}],)"
            R"("skins":[{"joints":[1,2,3,4]}],)"
            R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2,"JOINTS_1":3,"WEIGHTS_1":4}}]}],)"
            R"("accessors":[)"
            R"({"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)"
            R"({"bufferView":1,"componentType":5121,"count":3,"type":"VEC4"},)"
            R"({"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},)"
            R"({"bufferView":3,"componentType":5121,"count":3,"type":"VEC4"},)"
            R"({"bufferView":4,"componentType":5126,"count":3,"type":"VEC4"}],)"
            R"("bufferViews":[)"
            R"({"buffer":0,"byteOffset":0,"byteLength":36},)"
            R"({"buffer":0,"byteOffset":36,"byteLength":12},)"
            R"({"buffer":0,"byteOffset":48,"byteLength":48},)"
            R"({"buffer":0,"byteOffset":96,"byteLength":12},)"
            R"({"buffer":0,"byteOffset":108,"byteLength":48}],)"
            R"("buffers":[{"byteLength":156}]})";
    auto cooked = engine::gltf::cook_gltf(bytes_of(glb(json, bin)), {});
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
    auto model = engine::render::parse_model_blob(*cooked);
    ASSERT_TRUE(model.has_value());
    const engine::render::SkinInfluence& influence = model->primitives[0].mesh.influences[0];
    const float sum = 0.90f;
    EXPECT_NEAR(influence.weights[0], 0.40f / sum, 1e-4f);
    EXPECT_NEAR(influence.weights[1], 0.25f / sum, 1e-4f);
    EXPECT_NEAR(influence.weights[2], 0.15f / sum, 1e-4f);
    EXPECT_NEAR(influence.weights[3], 0.10f / sum, 1e-4f);
    EXPECT_EQ(influence.joints[0], 0);
    EXPECT_EQ(influence.joints[1], 1);
    EXPECT_EQ(influence.joints[2], 2);
    EXPECT_EQ(influence.joints[3], 3);
}

TEST(Model, CookerRejectsMorphDracoAndTooManyJoints) {
    std::vector<std::uint8_t> bin(36, 0);
    const char* morph =
            R"({"asset":{"version":"2.0"},"nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"targets":[{"POSITION":1}]}]}],)"
            R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],)"
            R"("bufferViews":[{"buffer":0,"byteLength":36}],"buffers":[{"byteLength":36}]})";
    EXPECT_NE(cook_error(glb(morph, bin)).message.find("morph"), std::string::npos);

    const char* draco =
            R"({"asset":{"version":"2.0"},"extensionsRequired":["KHR_draco_mesh_compression"],"nodes":[{"mesh":0}],)"
            R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
            R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],)"
            R"("bufferViews":[{"buffer":0,"byteLength":36}],"buffers":[{"byteLength":36}]})";
    EXPECT_NE(cook_error(glb(draco, bin)).message.find("extension"), std::string::npos);

    std::string nodes = "[";
    std::string joints = "[";
    for (int i = 0; i < 129; ++i) {
        if (i != 0) {
            nodes += ",";
            joints += ",";
        }
        nodes += "{}";
        joints += std::to_string(i);
    }
    nodes += "]";
    joints += "]";
    const std::string many = std::string(R"({"asset":{"version":"2.0"},"nodes":)") + nodes + R"(,"skins":[{"joints":)" +
                             joints + "}]}";
    EXPECT_NE(cook_error(glb(many, {})).message.find("128"), std::string::npos);
}

TEST(Model, LoopWrapsAndCrossfadeIsHalfway) {
    engine::render::ModelAsset model;
    model.nodes.push_back({});
    engine::render::ModelClip loop;
    loop.name = "loop";
    loop.duration = 1.0f / 30.0f;
    loop.key_count = 2;
    engine::render::ModelNodePose start;
    engine::render::ModelNodePose end;
    end.translation = glm::vec3(1, 0, 0);
    loop.samples = {start, end};
    model.clips.push_back(loop);

    const auto at_start = engine::render::sample_model_locals(model, "loop", 0.0f, true);
    const auto wrapped = engine::render::sample_model_locals(model, "loop", loop.duration + loop.duration * 0.01f, true);
    EXPECT_NEAR(at_start[0].translation.x, 0.0f, 1e-4f);
    EXPECT_NEAR(wrapped[0].translation.x, 0.01f, 1e-3f);

    engine::render::ModelClip a;
    a.name = "a";
    a.key_count = 1;
    a.samples.push_back({});
    engine::render::ModelClip b;
    b.name = "b";
    b.key_count = 1;
    engine::render::ModelNodePose turned;
    turned.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
    b.samples.push_back(turned);
    model.clips.push_back(a);
    model.clips.push_back(b);

    engine::render::ModelAnimator animator;
    animator.model = std::make_shared<engine::render::ModelAsset>(model);
    animator.play("a");
    engine::render::tick_model_animator(animator, 0.0f);
    animator.play("b", 1.0f);
    engine::render::tick_model_animator(animator, 0.5f);
    const glm::quat halfway = glm::slerp(glm::quat(1, 0, 0, 0), turned.rotation, 0.5f);
    EXPECT_NEAR(animator.locals[0].rotation.w, halfway.w, 1e-4f);
    EXPECT_NEAR(animator.locals[0].rotation.z, halfway.z, 1e-4f);
}

TEST(Model, InfluenceCountMustMatchVertices) {
    engine::render::MeshDesc desc;
    desc.vertices.resize(3);
    desc.influences.resize(2);
    EXPECT_FALSE(engine::render::mesh_desc_error(desc).empty());
}

TEST(Model, CodegenRejectsABadModelAndStagesABlob) {
    const auto dir = std::filesystem::temp_directory_path() / "wind_model_codegen";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::vector<std::uint8_t> bad = glb(R"({"asset":{"version":"2.0"},"extensionsRequired":["KHR_draco_mesh_compression"]})", {});
    {
        std::ofstream out(dir / "bad.glb", std::ios::binary);
        out.write(reinterpret_cast<const char*>(bad.data()), static_cast<std::streamsize>(bad.size()));
    }
    {
        std::ofstream meta(dir / "bad.glb.meta");
        meta << "guid = \"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb1\"\nimporter = \"model\"\n";
    }
    engine::gltf::register_gltf_cooker();
    const auto scanned = engine::codegen_scan(dir);
    ASSERT_FALSE(scanned.has_value());
    EXPECT_EQ(scanned.error().kind, engine::CodegenErrorKind::Model);

    std::vector<std::uint8_t> bin;
    for (float value : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) {
        append_f32(bin, value);
    }
    const char* json =
            R"({"asset":{"version":"2.0"},"nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
            R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],)"
            R"("bufferViews":[{"buffer":0,"byteLength":36}],"buffers":[{"byteLength":36}]})";
    const auto good = glb(json, bin);
    std::filesystem::remove(dir / "bad.glb");
    std::filesystem::remove(dir / "bad.glb.meta");
    {
        std::ofstream out(dir / "hero.glb", std::ios::binary);
        out.write(reinterpret_cast<const char*>(good.data()), static_cast<std::streamsize>(good.size()));
    }
    {
        std::ofstream meta(dir / "hero.glb.meta");
        meta << "guid = \"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb2\"\nimporter = \"model\"\n";
    }
    const auto stage = std::filesystem::temp_directory_path() / "wind_model_stage";
    std::filesystem::remove_all(stage);
    const auto staged = engine::codegen_stage(dir, stage);
    ASSERT_TRUE(staged.has_value()) << staged.error().message;
    {
        std::ifstream in(stage / "hero.glb", std::ios::binary);
        char magic[4]{};
        in.read(magic, 4);
        EXPECT_EQ(std::string(magic, 4), "WMOD");
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::remove_all(stage, ec);
}

TEST(Model, SpawnWritesPartsTheWorldSaveSkips) {
    std::vector<std::uint8_t> bin;
    for (float value : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) {
        append_f32(bin, value);
    }
    const char* json =
            R"({"asset":{"version":"2.0"},"nodes":[{"name":"hand","mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
            R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],)"
            R"("bufferViews":[{"buffer":0,"byteLength":36}],"buffers":[{"byteLength":36}]})";
    auto cooked = engine::gltf::cook_gltf(bytes_of(glb(json, bin)), {});
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;

    const auto dir = std::filesystem::temp_directory_path() / "wind_model_spawn";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    {
        std::ofstream out(dir / "hero.glb", std::ios::binary);
        out.write(reinterpret_cast<const char*>(cooked->data()), static_cast<std::streamsize>(cooked->size()));
    }
    constexpr std::string_view kModel = "ccccccccccccccccccccccccccccccc1";
    engine::CookedCatalog catalog;
    const auto builtin = std::filesystem::path{ENGINE_BUILTIN_ASSETS_DIR};
    catalog.add({engine::builtin::shader_lit, "shaders/lit.shader", engine::ImporterKind::Shader, {}, {}, builtin});
    catalog.add({engine::builtin::shader_unlit, "shaders/unlit.shader", engine::ImporterKind::Shader, {}, {}, builtin});
    catalog.add({engine::builtin::shader_skin_lit, "shaders/skin_lit.shader", engine::ImporterKind::Shader, {}, {}, builtin});
    catalog.add({engine::builtin::shader_skin_unlit, "shaders/skin_unlit.shader", engine::ImporterKind::Shader, {}, {}, builtin});
    catalog.add({engine::AssetId{kModel}, "hero.glb", engine::ImporterKind::Model, {}, {}, dir});

    fakes::QuietFatal fatal;
    engine::AssetsDb db(fatal);
    db.add_catalog(std::move(catalog));
    TestFactory factory;
    db.set_graphic_factory(&factory);

    engine::ecs::World world;
    const auto root = engine::ecs::spawn_model(world, engine::AssetId{kModel}, db);
    ASSERT_TRUE(root.has_value()) << root.error().message;
    EXPECT_NE(world.try_get<engine::render::ModelAnimator>(*root), nullptr);
    world.emplace<engine::ecs::ModelPlacement>(*root, engine::ecs::ModelPlacement{engine::AssetId{kModel}, {}});
    const auto joint = engine::ecs::joint_world(world, *root, "hand");
    ASSERT_TRUE(joint.has_value());

    const engine::ecs::WorldAsset saved = engine::ecs::capture_world_asset(world);
    ASSERT_EQ(saved.entities.size(), 1u);
    EXPECT_TRUE(saved.entities[0].components.contains("model"));
    std::filesystem::remove_all(dir);
}
