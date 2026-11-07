#include <gtest/gtest.h>

#include <engine/render/command_buffer.h>
#include <engine/render/commands.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>
#include <engine/render/primitives.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

class FakeMesh final : public engine::render::IMesh {
public:
    FakeMesh() : engine::render::IMesh(engine::render::make_quad()) {}
    explicit FakeMesh(const engine::render::MeshDesc& desc) : engine::render::IMesh(desc) {}

protected:
    bool replace(const engine::render::MeshDesc&) override {
        return true;
    }
};

class FakeMaterial final : public engine::render::IMaterial {
public:
    std::shared_ptr<engine::render::IShader> shader() const override {
        return {};
    }

    std::shared_ptr<engine::render::ITexture> texture(int) const override {
        return {};
    }

    glm::vec4 color() const override {
        return {1.0f, 1.0f, 1.0f, 1.0f};
    }

    engine::render::RenderState render_state() const override {
        return {.blend = engine::render::BlendMode::Opaque};
    }
};

engine::render::CmdDrawMesh make_mesh_cmd() {
    engine::render::CmdDrawMesh cmd;
    cmd.mesh = std::make_shared<FakeMesh>();
    cmd.material = std::make_shared<FakeMaterial>();
    return cmd;
}

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

    void operator()(const engine::render::CmdDrawParticles&) {
        kinds.emplace_back("particles");
    }

    void operator()(const engine::render::CmdDrawDecal&) {
        kinds.emplace_back("decal");
    }

    void operator()(const engine::render::CmdSetView&) {
        kinds.emplace_back("view");
    }

    void operator()(const engine::render::CmdPostProcess&) {
        kinds.emplace_back("post");
    }
};

template<typename T, typename = void>
struct has_shader_field : std::false_type {};

template<typename T>
struct has_shader_field<T, std::void_t<decltype(std::declval<T>().shader)>> : std::true_type {};

template<typename T, typename = void>
struct has_view_field : std::false_type {};

template<typename T>
struct has_view_field<T, std::void_t<decltype(std::declval<T>().view)>> : std::true_type {};

template<typename T, typename = void>
struct has_projection_field : std::false_type {};

template<typename T>
struct has_projection_field<T, std::void_t<decltype(std::declval<T>().projection)>> : std::true_type {};

template<typename T, typename = void>
struct has_texture_field : std::false_type {};

template<typename T>
struct has_texture_field<T, std::void_t<decltype(std::declval<T>().texture)>> : std::true_type {};

} // namespace

TEST(CommandBuffer, PushMeshAppendsToVector) {
    engine::render::CommandBuffer buffer;
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.size(), 0u);

    buffer.push(make_mesh_cmd());

    EXPECT_FALSE(buffer.empty());
    EXPECT_EQ(buffer.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(buffer[0]));
}

TEST(CommandBuffer, VariantHasExpectedCommands) {
    using engine::render::CmdDrawDecal;
    using engine::render::CmdDrawMesh;
    using engine::render::CmdDrawParticles;
    using engine::render::CmdDrawUI;
    using engine::render::CmdSetView;
    using engine::render::Command;

    static_assert(std::variant_size_v<Command> == 6);
    static_assert(std::is_same_v<std::variant_alternative_t<0, Command>, CmdDrawMesh>);
    static_assert(std::is_same_v<std::variant_alternative_t<1, Command>, CmdDrawUI>);
    static_assert(std::is_same_v<std::variant_alternative_t<2, Command>, CmdDrawParticles>);
    static_assert(std::is_same_v<std::variant_alternative_t<3, Command>, CmdDrawDecal>);
    static_assert(std::is_same_v<std::variant_alternative_t<4, Command>, CmdSetView>);
    static_assert(std::is_same_v<std::variant_alternative_t<5, Command>, engine::render::CmdPostProcess>);
    static_assert(!has_shader_field<CmdDrawMesh>::value);
    static_assert(!has_texture_field<CmdDrawMesh>::value);
    // The camera reaches draws only through CmdSetView.
    static_assert(!has_view_field<CmdDrawMesh>::value);
    static_assert(!has_projection_field<CmdDrawMesh>::value);
    static_assert(!has_view_field<CmdDrawParticles>::value);
    static_assert(!has_projection_field<CmdDrawParticles>::value);
    static_assert(!has_view_field<CmdDrawDecal>::value);
    static_assert(!has_projection_field<CmdDrawDecal>::value);
    static_assert(has_view_field<CmdSetView>::value);
    static_assert(has_projection_field<CmdSetView>::value);
    SUCCEED();
}

TEST(CommandBuffer, PushMeshThenUiPreservesFifo) {
    engine::render::CommandBuffer buffer;
    auto mesh_cmd = make_mesh_cmd();
    const auto material = mesh_cmd.material;
    buffer.push(std::move(mesh_cmd));

    engine::render::CmdDrawUI ui_cmd;
    ui_cmd.rect = {10.0f, 20.0f, 30.0f, 40.0f};
    buffer.push(ui_cmd);

    ASSERT_EQ(buffer.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(buffer[0]));
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawUI>(buffer[1]));

    const auto& fetched_mesh = std::get<engine::render::CmdDrawMesh>(buffer[0]);
    EXPECT_EQ(fetched_mesh.material, material);

    const auto& fetched_ui = std::get<engine::render::CmdDrawUI>(buffer[1]);
    EXPECT_FLOAT_EQ(fetched_ui.rect.x, 10.0f);
    EXPECT_FLOAT_EQ(fetched_ui.rect.y, 20.0f);
    EXPECT_FLOAT_EQ(fetched_ui.rect.w, 30.0f);
    EXPECT_FLOAT_EQ(fetched_ui.rect.h, 40.0f);
}

TEST(CommandBuffer, PushCommandVariantOverloadPreservesFifo) {
    engine::render::CommandBuffer buffer;
    engine::render::Command c1 = make_mesh_cmd();
    engine::render::Command c2 = engine::render::CmdDrawUI{{1.0f, 2.0f, 3.0f, 4.0f}};

    buffer.push(std::move(c1));
    buffer.push(std::move(c2));

    ASSERT_EQ(buffer.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawMesh>(buffer[0]));
    EXPECT_TRUE(std::holds_alternative<engine::render::CmdDrawUI>(buffer[1]));
}

TEST(CommandBuffer, ClearEmptiesBuffer) {
    engine::render::CommandBuffer buffer;
    buffer.push(make_mesh_cmd());
    buffer.push(engine::render::CmdDrawUI{{0.0f, 0.0f, 10.0f, 10.0f}});

    EXPECT_EQ(buffer.size(), 2u);
    buffer.clear();
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.size(), 0u);
}

TEST(CommandBuffer, ExecuteVisitsAllCommandsInOrder) {
    engine::render::CommandBuffer buffer;
    buffer.push(make_mesh_cmd());
    buffer.push(engine::render::CmdDrawUI{{5.0f, 6.0f, 7.0f, 8.0f}});
    buffer.push(make_mesh_cmd());

    RecordingBackend backend;
    buffer.execute(backend);

    ASSERT_EQ(backend.kinds.size(), 3u);
    EXPECT_EQ(backend.kinds[0], "mesh");
    EXPECT_EQ(backend.kinds[1], "ui");
    EXPECT_EQ(backend.kinds[2], "mesh");

    EXPECT_FLOAT_EQ(backend.last_ui_rect.x, 5.0f);
    EXPECT_FLOAT_EQ(backend.last_ui_rect.y, 6.0f);
    EXPECT_FLOAT_EQ(backend.last_ui_rect.w, 7.0f);
    EXPECT_FLOAT_EQ(backend.last_ui_rect.h, 8.0f);
}

TEST(CommandBuffer, PushParticlesPreservesFifo) {
    engine::render::CommandBuffer buffer;
    buffer.push(make_mesh_cmd());
    engine::render::CmdDrawParticles particles_cmd;
    particles_cmd.instances.push_back(engine::render::ParticleInstance{.position = {1.0f, 2.0f, 3.0f}});
    buffer.push(std::move(particles_cmd));
    buffer.push(engine::render::CmdDrawUI{{0.0f, 0.0f, 10.0f, 10.0f}});

    ASSERT_EQ(buffer.size(), 3u);
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawParticles>(buffer[1]));
    EXPECT_EQ(std::get<engine::render::CmdDrawParticles>(buffer[1]).instances.size(), 1u);

    RecordingBackend backend;
    buffer.execute(backend);
    ASSERT_EQ(backend.kinds.size(), 3u);
    EXPECT_EQ(backend.kinds[0], "mesh");
    EXPECT_EQ(backend.kinds[1], "particles");
    EXPECT_EQ(backend.kinds[2], "ui");
}

TEST(CommandBuffer, PushDecalPreservesFifo) {
    engine::render::CommandBuffer buffer;
    buffer.push(make_mesh_cmd());
    engine::render::CmdDrawDecal decal_cmd;
    decal_cmd.color = {1.0f, 0.5f, 0.2f, 1.0f};
    buffer.push(std::move(decal_cmd));
    buffer.push(engine::render::CmdDrawUI{{0.0f, 0.0f, 10.0f, 10.0f}});

    ASSERT_EQ(buffer.size(), 3u);
    ASSERT_TRUE(std::holds_alternative<engine::render::CmdDrawDecal>(buffer[1]));
    EXPECT_EQ(std::get<engine::render::CmdDrawDecal>(buffer[1]).color.g, 0.5f);

    RecordingBackend backend;
    buffer.execute(backend);
    ASSERT_EQ(backend.kinds.size(), 3u);
    EXPECT_EQ(backend.kinds[0], "mesh");
    EXPECT_EQ(backend.kinds[1], "decal");
    EXPECT_EQ(backend.kinds[2], "ui");
}

TEST(CommandBuffer, SetViewDefaultsToIdentityWithoutLights) {
    const engine::render::CmdSetView view;
    EXPECT_EQ(view.view, glm::mat4(1.0f));
    EXPECT_EQ(view.projection, glm::mat4(1.0f));
    EXPECT_EQ(view.camera_position, glm::vec3(0.0f));
    EXPECT_EQ(view.lights.count, 0);
    EXPECT_EQ(view.lights.ambient, glm::vec3(0.0f));
}

TEST(CommandBuffer, SetViewIsVisitedInOrder) {
    engine::render::CommandBuffer buffer;
    buffer.push(engine::render::CmdSetView{});
    buffer.push(make_mesh_cmd());
    buffer.push(engine::render::CmdSetView{});
    buffer.push(make_mesh_cmd());

    RecordingBackend backend;
    buffer.execute(backend);
    EXPECT_EQ(backend.kinds, (std::vector<std::string>{"view", "mesh", "view", "mesh"}));
}
