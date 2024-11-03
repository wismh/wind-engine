#pragma once

#include <engine/ecs/entity.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <memory>
#include <optional>

namespace engine::render {

struct Sprite {
    std::shared_ptr<ITexture> texture;
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    int layer = 0;
    int order_in_layer = 0;
    bool flip_x = false;
    bool flip_y = false;
    glm::vec2 tiling{1.0f, 1.0f};
    glm::vec2 offset{0.0f, 0.0f};
    glm::vec2 pixel_size{0.0f, 0.0f};
    float pixels_per_unit = 100.0f;
    glm::vec2 pivot{0.5f, 0.5f};

    // Optional overrides (defaults to builtin::mesh_quad and builtin::material_unlit when null)
    std::shared_ptr<IMesh> mesh;
    std::shared_ptr<IMaterial> material;
    std::optional<MaterialOverride> material_override;

    [[nodiscard]] glm::vec4 tinted_color() const {
        const glm::vec4 material_color =
                material ? material->color() : glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
        return multiply_instance_color(material_color, color);
    }
};

}
