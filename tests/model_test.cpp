#include <gtest/gtest.h>

#include <engine/builtin_ids.h>
#include <engine/core/time.h>
#include <engine/ecs/model.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/ecs/world_serializer.h>
#include <engine/render/graphic_factory.h>
#include <engine/render/model.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/meta.h>

#include "fixtures/cooked_shaders.h"
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
#include <span>
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

int cue_count(const engine::render::ModelAnimator& animator, std::string_view name) {
    int count = 0;
    for (const engine::render::ModelAnimCue& cue : animator.cues) {
        if (cue.name == name) {
            ++count;
        }
    }
    return count;
}

std::vector<std::uint8_t> attack_clip_glb() {
    std::vector<std::uint8_t> bin;
    append_f32(bin, 0.0f);
    append_f32(bin, 1.0f);
    for (float value : {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}) {
        append_f32(bin, value);
    }
    const char* json =
            R"({"asset":{"version":"2.0"},"nodes":[{}],)"
            R"("animations":[{"name":"attack","channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}],)"
            R"("samplers":[{"input":0,"output":1,"interpolation":"LINEAR"}]}],)"
            R"("accessors":[{"bufferView":0,"componentType":5126,"count":2,"type":"SCALAR"},)"
            R"({"bufferView":1,"componentType":5126,"count":2,"type":"VEC3"}],)"
            R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":8},{"buffer":0,"byteOffset":8,"byteLength":24}],)"
            R"("buffers":[{"byteLength":32}]})";
    return glb(json, std::move(bin));
}

engine::render::ModelAnimator attack_animator() {
    engine::render::ModelAsset model;
    model.nodes.push_back({});
    engine::render::ModelClip attack;
    attack.name = "attack";
    attack.duration = 1.0f;
    attack.key_count = 1;
    attack.samples.push_back({});
    attack.markers.push_back({0.0f, "start"});
    attack.markers.push_back({0.35f, "hit"});
    model.clips.push_back(std::move(attack));
    engine::render::ModelClip idle;
    idle.name = "idle";
    idle.duration = 1.0f;
    idle.key_count = 1;
    idle.samples.push_back({});
    idle.markers.push_back({0.2f, "step"});
    model.clips.push_back(std::move(idle));
    engine::render::ModelAnimator animator;
    animator.model = std::make_shared<engine::render::ModelAsset>(std::move(model));
    return animator;
}

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
    const auto builtin = cooked_builtin_assets();
    ASSERT_FALSE(builtin.empty());
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

TEST(Model, SpawnedClipFiresTheMarkerAtZero) {
    const engine::ModelMarkerMeta start{"attack", 0.0f, "start"};
    const auto cooked = engine::gltf::cook_gltf(bytes_of(attack_clip_glb()), {}, std::span{&start, 1});
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;

    const auto dir = std::filesystem::temp_directory_path() / "wind_model_spawn_cue";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    {
        std::ofstream out(dir / "hero.glb", std::ios::binary);
        out.write(reinterpret_cast<const char*>(cooked->data()), static_cast<std::streamsize>(cooked->size()));
    }
    constexpr std::string_view kModel = "ccccccccccccccccccccccccccccccc2";
    engine::CookedCatalog catalog;
    const auto builtin = cooked_builtin_assets();
    ASSERT_FALSE(builtin.empty());
    catalog.add({engine::builtin::shader_lit, "shaders/lit.shader", engine::ImporterKind::Shader, {}, {}, builtin});
    catalog.add({engine::builtin::shader_unlit, "shaders/unlit.shader", engine::ImporterKind::Shader, {}, {}, builtin});
    catalog.add(
            {engine::builtin::shader_skin_lit, "shaders/skin_lit.shader", engine::ImporterKind::Shader, {}, {}, builtin});
    catalog.add(
            {engine::builtin::shader_skin_unlit, "shaders/skin_unlit.shader", engine::ImporterKind::Shader, {}, {}, builtin});
    catalog.add({engine::AssetId{kModel}, "hero.glb", engine::ImporterKind::Model, {}, {}, dir});

    fakes::QuietFatal fatal;
    engine::AssetsDb db(fatal);
    db.add_catalog(std::move(catalog));
    TestFactory factory;
    db.set_graphic_factory(&factory);

    engine::ecs::World world;
    const auto root = engine::ecs::spawn_model(world, engine::AssetId{kModel}, db);
    ASSERT_TRUE(root.has_value()) << root.error().message;
    engine::render::ModelAnimator* animator = world.try_get<engine::render::ModelAnimator>(*root);
    ASSERT_NE(animator, nullptr);
    ASSERT_FALSE(animator->layers.empty());
    EXPECT_TRUE(animator->layers[0].cue_from_start);
    engine::render::tick_model_animator(*animator, 0.05f);
    EXPECT_EQ(cue_count(*animator, "start"), 1);
    std::filesystem::remove_all(dir);
}

TEST(Model, MarkerMetaMatchesTextureSettings) {
    constexpr std::string_view kHead =
            "guid = \"0123456789abcdef0123456789abcdef\"\nimporter = \"model\"\n";
    const auto bare = engine::parse_asset_meta(kHead);
    ASSERT_TRUE(bare.has_value());
    EXPECT_TRUE(bare->model.markers.empty());

    const auto parsed = engine::parse_asset_meta(std::string(kHead) +
            "[[markers]]\nclip = \"attack\"\ntime = 0.35\nname = \"hit\"\n"
            "[[markers]]\nclip = \"walk\"\ntime = 0\nname = \"step\"\n"
            "[[markers]]\nclip = \"walk\"\ntime = 0.5\nname = \"step\"\n");
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->model.markers.size(), 3u);
    EXPECT_EQ(parsed->model.markers[0], (engine::ModelMarkerMeta{"attack", 0.35f, "hit"}));
    EXPECT_EQ(parsed->model.markers[2].name, "step");

    EXPECT_EQ(engine::parse_asset_meta(std::string(kHead) + "[[markers]]\nclip = \"\"\ntime = 0\nname = \"hit\"\n").error(),
            engine::MetaError::InvalidField);
    EXPECT_EQ(engine::parse_asset_meta(std::string(kHead) + "[[markers]]\nclip = \"attack\"\ntime = -0.1\nname = \"hit\"\n")
                      .error(),
            engine::MetaError::InvalidField);
    EXPECT_EQ(engine::parse_asset_meta("guid = \"0123456789abcdef0123456789abcdef\"\nimporter = \"texture\"\n"
                                       "[[markers]]\nclip = \"attack\"\ntime = 0\nname = \"hit\"\n")
                      .error(),
            engine::MetaError::InvalidField);

    engine::CookedCatalog catalog;
    engine::CatalogEntry entry;
    entry.guid = engine::AssetId{"0123456789abcdef0123456789abcdef"};
    entry.relative_path = "hero.glb";
    entry.importer = engine::ImporterKind::Model;
    entry.model = parsed->model;
    catalog.add(entry);
    const auto loaded = engine::parse_cooked_catalog(catalog.serialize());
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->entries().size(), 1u);
    EXPECT_EQ(loaded->entries()[0].model.markers, parsed->model.markers);
}

TEST(Model, CookRejectsAMarkerTheClipDoesNotHave) {
    const auto file = attack_clip_glb();
    const engine::ModelMarkerMeta unknown{"missing", 0.1f, "hit"};
    const auto missing = engine::gltf::cook_gltf(bytes_of(file), {}, std::span{&unknown, 1});
    ASSERT_FALSE(missing.has_value());
    EXPECT_NE(missing.error().message.find("unknown"), std::string::npos);

    const engine::ModelMarkerMeta late{"attack", 1.0f, "hit"};
    const auto outside = engine::gltf::cook_gltf(bytes_of(file), {}, std::span{&late, 1});
    ASSERT_FALSE(outside.has_value());
    EXPECT_NE(outside.error().message.find("outside"), std::string::npos);

    const engine::ModelMarkerMeta hit{"attack", 0.35f, "hit"};
    const auto cooked = engine::gltf::cook_gltf(bytes_of(file), {}, std::span{&hit, 1});
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
    auto model = engine::render::parse_model_blob(*cooked);
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->clips.size(), 1u);
    ASSERT_EQ(model->clips[0].markers.size(), 1u);
    EXPECT_EQ(model->clips[0].markers[0].name, "hit");
    EXPECT_FLOAT_EQ(model->clips[0].markers[0].time, 0.35f);

    auto version_one = *cooked;
    version_one[4] = 1;
    EXPECT_FALSE(engine::render::parse_model_blob(version_one).has_value());
}

TEST(Model, MarkersCrossTheActiveClipOnly) {
    engine::render::ModelAnimator animator = attack_animator();
    animator.play("attack");
    engine::render::tick_model_animator(animator, 0.2f);
    EXPECT_EQ(cue_count(animator, "start"), 1);
    EXPECT_EQ(cue_count(animator, "hit"), 0);

    engine::render::tick_model_animator(animator, 0.2f);
    EXPECT_EQ(cue_count(animator, "start"), 0);
    EXPECT_EQ(cue_count(animator, "hit"), 1);
    EXPECT_EQ(animator.cues[0].clip, "attack");

    engine::render::tick_model_animator(animator, 0.0f);
    EXPECT_TRUE(animator.cues.empty());

    animator.layers[0].playing = false;
    engine::render::tick_model_animator(animator, 1.0f);
    EXPECT_TRUE(animator.cues.empty());
    EXPECT_TRUE(animator.layers[0].cue_from_start == false);

    animator = attack_animator();
    animator.play("attack");
    animator.layers[0].playing = false;
    engine::render::tick_model_animator(animator, 0.5f);
    EXPECT_TRUE(animator.cues.empty());
    EXPECT_TRUE(animator.layers[0].cue_from_start);
    animator.layers[0].playing = true;
    engine::render::tick_model_animator(animator, 0.2f);
    EXPECT_EQ(cue_count(animator, "start"), 1);
    EXPECT_EQ(cue_count(animator, "hit"), 0);

    animator = attack_animator();
    animator.play("attack");
    animator.layers[0].loop = false;
    engine::render::tick_model_animator(animator, 3.0f);
    EXPECT_EQ(cue_count(animator, "start"), 1);
    EXPECT_EQ(cue_count(animator, "hit"), 1);
    EXPECT_FALSE(animator.layers[0].playing);
    engine::render::tick_model_animator(animator, 1.0f);
    EXPECT_TRUE(animator.cues.empty());

    animator = attack_animator();
    animator.play("attack");
    engine::render::tick_model_animator(animator, 0.0f);
    animator.layers[0].time = 0.2f;
    engine::render::tick_model_animator(animator, 2.0f);
    EXPECT_EQ(cue_count(animator, "hit"), 2);
    EXPECT_EQ(cue_count(animator, "start"), 2);

    animator = attack_animator();
    animator.play("attack");
    engine::render::tick_model_animator(animator, 0.0f);
    animator.play("idle", 1.0f);
    engine::render::tick_model_animator(animator, 0.6f);
    EXPECT_EQ(cue_count(animator, "hit"), 0);
    EXPECT_EQ(cue_count(animator, "step"), 1);
    EXPECT_EQ(animator.cues[0].clip, "idle");
}

TEST(Model, AnimationEventIsReadOnceOnTheFrame) {
    engine::render::ModelAsset model;
    model.nodes.push_back({});
    engine::render::ModelClip attack;
    attack.name = "attack";
    attack.duration = 1.0f;
    attack.key_count = 1;
    attack.samples.push_back({});
    attack.markers.push_back({0.35f, "hit"});
    model.clips.push_back(std::move(attack));

    engine::ecs::World world;
    engine::register_simulation_systems(world);
    world.ctx<engine::Time>().delta_time = 0.4f;
    const engine::ecs::Entity root = world.create();
    engine::render::ModelAnimator animator;
    animator.model = std::make_shared<engine::render::ModelAsset>(std::move(model));
    animator.play("attack");
    world.emplace<engine::render::ModelAnimator>(root, std::move(animator));

    auto& cursor = world.ctx<engine::ecs::EventCursor<engine::ecs::ModelAnimEvent>>();
    world.run(engine::ecs::Schedule::Frame);
    int hits = 0;
    for (const engine::ecs::ModelAnimEvent& event :
            engine::ecs::EventReader<engine::ecs::ModelAnimEvent>(world, cursor)) {
        EXPECT_EQ(event.entity, root);
        EXPECT_EQ(event.clip, "attack");
        EXPECT_EQ(event.name, "hit");
        ++hits;
    }
    EXPECT_EQ(hits, 1);

    world.ctx<engine::Time>().delta_time = 0.1f;
    world.run(engine::ecs::Schedule::Frame);
    int again = 0;
    for (const engine::ecs::ModelAnimEvent& event :
            engine::ecs::EventReader<engine::ecs::ModelAnimEvent>(world, cursor)) {
        (void)event;
        ++again;
    }
    EXPECT_EQ(again, 0);
}

TEST(Model, CodegenCooksMarkersFromTheSidecar) {
    const auto dir = std::filesystem::temp_directory_path() / "wind_model_markers";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto file = attack_clip_glb();
    {
        std::ofstream out(dir / "hero.glb", std::ios::binary);
        out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    }
    {
        std::ofstream meta(dir / "hero.glb.meta");
        meta << "guid = \"ddddddddddddddddddddddddddddddd1\"\nimporter = \"model\"\n\n"
                "[[markers]]\nclip = \"missing\"\ntime = 0.1\nname = \"hit\"\n";
    }
    engine::gltf::register_gltf_cooker();
    const auto scanned = engine::codegen_scan(dir);
    ASSERT_FALSE(scanned.has_value());
    EXPECT_EQ(scanned.error().kind, engine::CodegenErrorKind::Model);
    EXPECT_NE(scanned.error().message.find("unknown"), std::string::npos);

    {
        std::ofstream meta(dir / "hero.glb.meta");
        meta << "guid = \"ddddddddddddddddddddddddddddddd1\"\nimporter = \"model\"\n\n"
                "[[markers]]\nclip = \"attack\"\ntime = 0.35\nname = \"hit\"\n";
    }
    const auto stage = std::filesystem::temp_directory_path() / "wind_model_markers_stage";
    std::filesystem::remove_all(stage);
    const auto staged = engine::codegen_stage(dir, stage);
    ASSERT_TRUE(staged.has_value()) << staged.error().message;
    std::ifstream in(stage / "hero.glb", std::ios::binary);
    const std::vector<std::uint8_t> blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto model = engine::render::parse_model_blob(blob);
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->clips.size(), 1u);
    ASSERT_EQ(model->clips[0].markers.size(), 1u);
    EXPECT_EQ(model->clips[0].markers[0].name, "hit");
    EXPECT_FLOAT_EQ(model->clips[0].markers[0].time, 0.35f);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::remove_all(stage, ec);
}

engine::render::ModelAsset masked_body() {
    engine::render::ModelAsset model;
    model.nodes.push_back(engine::render::ModelNode{.name = "hips"});
    model.nodes.push_back(engine::render::ModelNode{.name = "spine", .parent = 0});
    model.nodes.push_back(engine::render::ModelNode{.name = "head", .parent = 1});
    model.nodes.push_back(engine::render::ModelNode{.name = "leg", .parent = 0});
    const auto samples = [](glm::vec3 hips, glm::vec3 spine, glm::vec3 head, glm::vec3 leg) {
        std::vector<engine::render::ModelNodePose> poses(4);
        poses[0].translation = hips;
        poses[1].translation = spine;
        poses[2].translation = head;
        poses[3].translation = leg;
        return poses;
    };
    engine::render::ModelClip walk;
    walk.name = "walk";
    walk.duration = 1.0f;
    walk.key_count = 1;
    walk.samples = samples({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, -1.0f, 0.0f});
    engine::render::ModelClip hit;
    hit.name = "hit";
    hit.duration = 1.0f;
    hit.key_count = 1;
    hit.samples = samples({9.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}, {0.0f, 0.0f, 4.0f}, {5.0f, 5.0f, 5.0f});
    model.clips.push_back(std::move(walk));
    model.clips.push_back(std::move(hit));
    return model;
}

TEST(Model, OverrideMaskKeepsTheUnmaskedBones) {
    engine::render::ModelAnimator animator;
    animator.model = std::make_shared<engine::render::ModelAsset>(masked_body());
    animator.play("walk");
    animator.play_layer(1, "hit");
    animator.layers[1].mask = "spine";
    engine::render::tick_model_animator(animator, 0.0f);

    EXPECT_NEAR(animator.locals[0].translation.x, 0.0f, 1e-4f);
    EXPECT_NEAR(animator.locals[3].translation.y, -1.0f, 1e-4f);
    EXPECT_NEAR(animator.locals[1].translation.x, 0.0f, 1e-4f);
    EXPECT_NEAR(animator.locals[1].translation.y, 2.0f, 1e-4f);
    EXPECT_NEAR(animator.locals[2].translation.z, 4.0f, 1e-4f);

    animator.layers[1].weight = 0.5f;
    engine::render::tick_model_animator(animator, 0.0f);
    EXPECT_NEAR(animator.locals[1].translation.x, 0.5f, 1e-4f);
    EXPECT_NEAR(animator.locals[1].translation.y, 1.0f, 1e-4f);
    EXPECT_NEAR(animator.locals[3].translation.y, -1.0f, 1e-4f);
}

TEST(Model, AdditiveLayerAddsTheRestDelta) {
    engine::render::ModelAnimator animator;
    animator.model = std::make_shared<engine::render::ModelAsset>(masked_body());
    animator.play("walk");
    animator.play_layer(1, "hit");
    animator.layers[1].mask = "spine";
    animator.layers[1].blend = engine::render::ModelBlend::Additive;
    engine::render::tick_model_animator(animator, 0.0f);

    EXPECT_NEAR(animator.locals[1].translation.x, 1.0f, 1e-4f);
    EXPECT_NEAR(animator.locals[1].translation.y, 2.0f, 1e-4f);
    EXPECT_NEAR(animator.locals[2].translation.z, 5.0f, 1e-4f);
    EXPECT_NEAR(animator.locals[0].translation.x, 0.0f, 1e-4f);
    EXPECT_NEAR(animator.locals[3].translation.y, -1.0f, 1e-4f);
}

TEST(Model, SilentLayerSendsNoMarkers) {
    engine::render::ModelAnimator animator = attack_animator();
    animator.play("idle");
    animator.play_layer(1, "attack");
    animator.layers[1].weight = 0.0f;
    engine::render::tick_model_animator(animator, 0.5f);
    EXPECT_EQ(cue_count(animator, "hit"), 0);
    EXPECT_EQ(cue_count(animator, "start"), 0);
    EXPECT_EQ(cue_count(animator, "step"), 1);
    EXPECT_NEAR(animator.layers[1].time, 0.5f, 1e-4f);

    animator.layers[1].weight = 1.0f;
    animator.play_layer(1, "attack");
    engine::render::tick_model_animator(animator, 0.0f);
    EXPECT_EQ(cue_count(animator, "start"), 1);
    animator.play_layer(1, "idle", 1.0f);
    engine::render::tick_model_animator(animator, 0.4f);
    EXPECT_EQ(cue_count(animator, "hit"), 0);
    EXPECT_EQ(cue_count(animator, "step"), 1);
    EXPECT_EQ(animator.cues[0].clip, "idle");

    animator = attack_animator();
    animator.play("attack");
    animator.layers[0].mask = "no-such-bone";
    engine::render::tick_model_animator(animator, 0.5f);
    EXPECT_TRUE(animator.cues.empty());
    EXPECT_NEAR(animator.layers[0].time, 0.5f, 1e-4f);
}

TEST(Model, ImpulseMovesOnlyTheHitBoneThenSettles) {
    engine::render::ModelAsset model;
    engine::render::ModelNode arm;
    arm.name = "arm";
    arm.parent = 0;
    arm.translation = glm::vec3{0.0f, 1.0f, 0.0f};
    engine::render::ModelNode leg;
    leg.name = "leg";
    leg.parent = 0;
    leg.translation = glm::vec3{0.0f, -1.0f, 0.0f};
    model.nodes.push_back(engine::render::ModelNode{.name = "body"});
    model.nodes.push_back(arm);
    model.nodes.push_back(leg);

    engine::render::ModelAnimator animator;
    animator.model = std::make_shared<engine::render::ModelAsset>(std::move(model));
    engine::render::tick_model_animator(animator, 0.0f);
    engine::render::impulse_joint(animator, "missing", glm::vec3{1.0f, 0.0f, 0.0f}, 5.0f);
    EXPECT_TRUE(animator.springs.empty());

    engine::render::impulse_joint(animator, "arm", glm::vec3{1.0f, 0.0f, 0.0f}, 5.0f);
    engine::render::impulse_joint(animator, "arm", glm::vec3{1.0f, 0.0f, 0.0f}, 5.0f);
    ASSERT_EQ(animator.springs.size(), 1u);
    EXPECT_NEAR(animator.springs[0].linear_velocity.x, 10.0f, 1e-3f);

    engine::render::tick_model_animator(animator, 1.0f / 60.0f);
    EXPECT_GT(animator.locals[1].translation.x, 0.01f);
    EXPECT_NEAR(animator.locals[0].translation.x, 0.0f, 1e-4f);
    EXPECT_NEAR(animator.locals[2].translation.x, 0.0f, 1e-4f);
    EXPECT_NEAR(animator.locals[2].translation.y, -1.0f, 1e-4f);

    for (int i = 0; i < 180; ++i) {
        engine::render::tick_model_animator(animator, 1.0f / 60.0f);
    }
    EXPECT_NEAR(animator.locals[1].translation.x, 0.0f, 1e-2f);
    EXPECT_NEAR(animator.locals[1].translation.y, 1.0f, 1e-2f);
    EXPECT_TRUE(animator.springs.empty());
}

TEST(Model, NearestJointUsesThePosedWorldPosition) {
    engine::render::ModelAsset model;
    engine::render::ModelNode far;
    far.name = "far";
    far.translation = glm::vec3{10.0f, 0.0f, 0.0f};
    model.nodes.push_back(engine::render::ModelNode{.name = "near"});
    model.nodes.push_back(far);

    engine::ecs::World world;
    const engine::ecs::Entity root = world.create();
    engine::Transform placed;
    placed.position = glm::vec3{100.0f, 0.0f, 0.0f};
    world.emplace<engine::Transform>(root, placed);
    engine::render::ModelAnimator animator;
    animator.model = std::make_shared<engine::render::ModelAsset>(std::move(model));
    engine::render::tick_model_animator(animator, 0.0f);
    world.emplace<engine::render::ModelAnimator>(root, std::move(animator));

    EXPECT_EQ(engine::ecs::nearest_joint(world, root, glm::vec3{100.0f, 0.0f, 0.0f}), "near");
    EXPECT_EQ(engine::ecs::nearest_joint(world, root, glm::vec3{109.0f, 1.0f, 0.0f}), "far");
    EXPECT_FALSE(engine::ecs::nearest_joint(world, world.create(), glm::vec3{0.0f}).has_value());
}
