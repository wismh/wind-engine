#include <gtest/gtest.h>

#include <engine/render/command_buffer.h>
#include <engine/render/commands.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>

#include <glm/vec4.hpp>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

class FakeMesh final : public engine::render::IMesh {};

class FakeMaterial final : public engine::render::IMaterial {
public:
    std::shared_ptr<engine::render::IShader> Shader() const override {
        return {};
    }

    std::shared_ptr<engine::render::ITexture> Texture(int) const override {
        return {};
    }

    glm::vec4 Color() const override {
        return {1.0f, 1.0f, 1.0f, 1.0f};
    }

    engine::render::BlendMode Blend() const override {
        return engine::render::BlendMode::Opaque;
    }
};

struct RecordingBackend {
    std::vector<std::string> kinds;
    engine::render::Rect last_ui_rect{};

    void operator()(const engine::render::CmdDrawMesh&) {
        kinds.emplace_back("mesh");
    }

    void operator()(const engine::render::CmdDrawUI& cmd) {
        kinds.emplace_back("ui");
        last_ui_rect = cmd.rect;
    }
};

template<typename T, typename = void>
struct has_shader_field : std::false_type {};

template<typename T>
struct has_shader_field<T, std::void_t<decltype(std::declval<T>().shader)>> : std::true_type {};

template<typename T, typename = void>
struct has_texture_field : std::false_type {};

template<typename T>
struct has_texture_field<T, std::void_t<decltype(std::declval<T>().texture)>> : std::true_type {};

engine::render::CmdDrawMesh make_mesh_cmd() {
    engine::render::CmdDrawMesh cmd;
    cmd.mesh = std::make_shared<FakeMesh>();
    cmd.material = std::make_shared<FakeMaterial>();
    cmd.color = {0.2f, 0.4f, 0.6f, 1.0f};
    return cmd;
}

}

TEST(CommandBuffer, VariantHasOnlyMeshAndUi) {
    using engine::render::CmdDrawMesh;
    using engine::render::CmdDrawUI;
    using engine::render::Command;

    static_assert(std::variant_size_v<Command> == 2);
    static_assert(std::is_same_v<std::variant_alternative_t<0, Command>, CmdDrawMesh>);
    static_assert(std::is_same_v<std::variant_alternative_t<1, Command>, CmdDrawUI>);
    static_assert(!has_shader_field<CmdDrawMesh>::value);
    static_assert(!has_texture_field<CmdDrawMesh>::value);
    SUCCEED();
}

TEST(CommandBuffer, PushMeshThenUiPreservesFifo) {
    engine::render::CommandBuffer buffer;
    auto mesh_cmd = make_mesh_cmd();
    const auto material = mesh_cmd.material;
    const engine::render::Rect ui_rect{1.0f, 2.0f, 3.0f, 4.0f};

    buffer.push(std::move(mesh_cmd));
    buffer.push(engine::render::CmdDrawUI{ui_rect});

    ASSERT_EQ(buffer.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(buffer[0]));
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawUI>(buffer[1]));

    const auto& drawn = std::get<engine::render::CmdDrawMesh>(buffer[0]);
    EXPECT_EQ(drawn.material, material);
    EXPECT_NE(drawn.material, nullptr);
    EXPECT_EQ(std::get<engine::render::CmdDrawUI>(buffer[1]).rect, ui_rect);

    RecordingBackend backend;
    buffer.execute(backend);
    ASSERT_EQ(backend.kinds.size(), 2u);
    EXPECT_EQ(backend.kinds[0], "mesh");
    EXPECT_EQ(backend.kinds[1], "ui");
    EXPECT_EQ(backend.last_ui_rect, ui_rect);
}

TEST(CommandBuffer, UiComesAfterWorld) {
    engine::render::CommandBuffer buffer;
    buffer.push(make_mesh_cmd());
    buffer.push(make_mesh_cmd());
    buffer.push(engine::render::CmdDrawUI{{0.0f, 0.0f, 100.0f, 50.0f}});

    ASSERT_EQ(buffer.size(), 3u);
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(buffer[0]));
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(buffer[1]));
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawUI>(buffer[2]));
}

TEST(CommandBuffer, ClearEmptiesBetweenFrames) {
    engine::render::CommandBuffer buffer;
    buffer.push(make_mesh_cmd());
    buffer.push(engine::render::CmdDrawUI{{0.0f, 0.0f, 8.0f, 8.0f}});
    ASSERT_EQ(buffer.size(), 2u);

    buffer.clear();
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.size(), 0u);

    buffer.push(engine::render::CmdDrawUI{{9.0f, 9.0f, 1.0f, 1.0f}});
    ASSERT_EQ(buffer.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawUI>(buffer[0]));
    EXPECT_EQ(std::get<engine::render::CmdDrawUI>(buffer[0]).rect.x, 9.0f);
}
