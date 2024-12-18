#pragma once

#include <engine/ecs/entity.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>

#include <glm/vec4.hpp>

#include <memory>

namespace engine::render {

struct Sprite {
    std::shared_ptr<ITexture> texture;
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    int layer = 0;
    int order_in_layer = 0;
    bool flip_x = false;
    bool flip_y = false;

    // Optional overrides (defaults to builtin::mesh_quad and builtin::material_unlit when null)
    std::shared_ptr<IMesh> mesh;
    std::shared_ptr<IMaterial> material;

    [[nodiscard]] glm::vec4 tinted_color() const {
        const glm::vec4 material_color =
                material ? material->color() : glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
        return multiply_instance_color(material_color, color);
    }
};

}
