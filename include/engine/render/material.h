#pragma once

// docs/tech/features/Materials and Sort.md

#include <engine/render/graphics.h>

#include <glm/vec4.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::render {

enum class BlendMode {
    Opaque,
    Alpha,
    Additive,
};

// Which triangle faces are discarded. Counter-clockwise winding (as seen by the camera) is the front face.
enum class CullMode {
    None,
    Back,
    Front,
};

// Fixed-function state a mesh draw runs with. The default is the 2D state: alpha blend, no depth test, no depth
// write, no culling. A 3D opaque material is `{.blend = BlendMode::Opaque, .depth_test = true, .depth_write = true,
// .cull = CullMode::Back}`. Opaque blend plus depth write puts a draw in the opaque queue (`draw_sort.h`).
struct RenderState {
    BlendMode blend = BlendMode::Alpha;
    bool depth_test = false;
    bool depth_write = false;
    CullMode cull = CullMode::None;

    constexpr bool operator==(const RenderState&) const noexcept = default;
};

class IMaterial {
public:
    virtual ~IMaterial() = default;
    virtual std::shared_ptr<IShader> shader() const = 0;
    virtual std::shared_ptr<ITexture> texture(int slot) const = 0;
    virtual glm::vec4 color() const = 0;
    virtual RenderState render_state() const = 0;
};

class Material final : public IMaterial {
public:
    Material(std::shared_ptr<IShader> shader, std::shared_ptr<ITexture> albedo = nullptr,
            glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}, RenderState state = {})
        : shader_(std::move(shader))
        , albedo_(std::move(albedo))
        , color_(color)
        , state_(state) {}

    std::shared_ptr<IShader> shader() const override {
        return shader_;
    }

    std::shared_ptr<ITexture> texture(int slot) const override {
        if (slot != 0) {
            return {};
        }
        return albedo_;
    }

    glm::vec4 color() const override {
        return color_;
    }

    RenderState render_state() const override {
        return state_;
    }

private:
    std::shared_ptr<IShader> shader_;
    std::shared_ptr<ITexture> albedo_;
    glm::vec4 color_{1.0f, 1.0f, 1.0f, 1.0f};
    RenderState state_;
};

// Per-instance override of a material's texture(0) and/or named shader vec4 uniforms,
// applied on top of the material a Renderable/Sprite already references (no new IMaterial needed
// per unique texture or effect). Not a CmdCustomDraw-style callback: still data, consumed by the
// existing named-uniform setters on the backend's shader.
struct MaterialOverride {
    std::shared_ptr<ITexture> albedo;
    std::vector<std::pair<std::string, glm::vec4>> vec4_params;

    // Replaces the value if `name` is already present, otherwise appends it.
    void set_vec4(std::string_view name, const glm::vec4& value);
};

// A `.mat` file. Blend defaults to opaque (unlike `RenderState{}`); depth and cull default to the 2D state.
struct MaterialDesc {
    std::string shader;
    RenderState state{.blend = BlendMode::Opaque};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    std::string albedo;
};

[[nodiscard]] std::optional<MaterialDesc> parse_material(std::string_view toml_text);
[[nodiscard]] std::optional<MaterialDesc> parse_material_file(std::string_view path);

[[nodiscard]] inline glm::vec4 multiply_instance_color(glm::vec4 material_color, glm::vec4 instance_color) {
    return material_color * instance_color;
}

}
