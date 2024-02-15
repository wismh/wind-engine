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
