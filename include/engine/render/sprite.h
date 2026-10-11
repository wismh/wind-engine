#pragma once

// docs/tech/features/Materials and Sort.md

#include <engine/ecs/entity.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <memory>
#include <optional>
#include <string_view>

namespace engine::render {

struct SpriteAnimationFrame;

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

    // A sprite showing `frame`: the frame's texture, UV region (`tiling`, `offset`), `pixel_size`, `pixels_per_unit`, and
    // `pivot`; everything else (tint, layer, flips, mesh, material) is the default. For spawning an animated or
    // atlas-frame entity without copying fields by hand.
    [[nodiscard]] static Sprite from_frame(const SpriteAnimationFrame& frame);

    // Copies those same frame-owned fields onto this sprite and leaves tint, layer, flips, mesh, material, and
    // override alone. The animation system calls this for every frame it shows.
    void apply_frame(const SpriteAnimationFrame& frame);

    // Sets one named vec4 shader uniform for this sprite's draws, once: it stays until set again or cleared, and no
    // other sprite that shares the material sees it. See `MaterialOverride`.
    void set_param(std::string_view name, const glm::vec4& value) {
        if (!material_override) {
            material_override.emplace();
        }
        material_override->set_vec4(name, value);
    }

    // Removes every per-sprite override (uniforms and albedo).
    void clear_overrides() { material_override.reset(); }

    [[nodiscard]] glm::vec4 tinted_color() const {
        const glm::vec4 material_color =
                material ? material->color() : glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
        return multiply_instance_color(material_color, color);
    }
};

}
