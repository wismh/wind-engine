#pragma once

// docs/tech/features/Decals.md

#include <engine/render/graphics.h>
#include <engine/render/material.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <memory>
#include <optional>
#include <string_view>

namespace engine::render {

struct Decal {
    // Optional explicit mesh. When null, uses builtin::mesh_cube (local extent [-0.5, 0.5]).
    std::shared_ptr<IMesh> mesh;
    // Optional explicit material. When null and `texture` is set, uses builtin::shader_decal with that texture.
    // The cached state is alpha, no depth test, no depth write, cull front. No material and no texture is fatal.
    std::shared_ptr<IMaterial> material;
    std::shared_ptr<ITexture> texture;
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec2 uv_scale{1.0f, 1.0f};
    glm::vec2 uv_offset{0.0f, 0.0f};
    int layer = 0;
    int order_in_layer = 0;

    // Angle cutoff in cosine threshold (0.0 means off; > 0 fades out as surface normal diverges from projection axis).
    float angle_cutoff = 0.0f;
    float normal_fade = 0.2f;

    std::optional<MaterialOverride> material_override;
    bool frustum_cull = true;

    // Sets one named vec4 shader uniform for this decal's draw.
    void set_param(std::string_view name, const glm::vec4& value) {
        if (!material_override) {
            material_override.emplace();
        }
        material_override->set_vec4(name, value);
    }

    void clear_overrides() noexcept {
        material_override.reset();
    }
};

} // namespace engine::render
