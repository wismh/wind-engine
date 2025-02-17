#pragma once

#include <engine/render/graphics.h>

#include <glm/vec4.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace engine::render {

enum class BlendMode {
    Opaque,
    Alpha,
    Additive,
};

class IMaterial {
public:
    virtual ~IMaterial() = default;
    virtual std::shared_ptr<IShader> shader() const = 0;
    virtual std::shared_ptr<ITexture> texture(int slot) const = 0;
    virtual glm::vec4 color() const = 0;
    virtual BlendMode blend() const = 0;
};

class Material final : public IMaterial {
public:
    Material(std::shared_ptr<IShader> shader, std::shared_ptr<ITexture> albedo = nullptr,
            glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}, BlendMode blend = BlendMode::Alpha)
        : shader_(std::move(shader))
        , albedo_(std::move(albedo))
        , color_(color)
        , blend_(blend) {}

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

    BlendMode blend() const override {
        return blend_;
    }

private:
    std::shared_ptr<IShader> shader_;
    std::shared_ptr<ITexture> albedo_;
    glm::vec4 color_{1.0f, 1.0f, 1.0f, 1.0f};
    BlendMode blend_ = BlendMode::Alpha;
};

struct MaterialDesc {
    std::string shader;
    BlendMode blend = BlendMode::Opaque;
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    std::string albedo;
};

[[nodiscard]] std::optional<MaterialDesc> parse_material(std::string_view toml_text);
[[nodiscard]] std::optional<MaterialDesc> parse_material_file(std::string_view path);

[[nodiscard]] inline glm::vec4 multiply_instance_color(glm::vec4 material_color, glm::vec4 instance_color) {
    return material_color * instance_color;
}

}
