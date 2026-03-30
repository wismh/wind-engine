#pragma once

// docs/tech/features/Materials and Sort.md

#include <engine/ecs/entity.h>
#include <engine/render/draw_sort.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace engine::render {

struct Renderable {
    std::shared_ptr<IMesh> mesh;
    std::shared_ptr<IMaterial> material;
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    int layer = 0;
    int order_in_layer = 0;
    std::optional<MaterialOverride> material_override;

    // Sets one named vec4 shader uniform for this renderable's draws, once: it stays until set again or cleared, and
    // no other renderable that shares the material sees it. See `MaterialOverride`.
    void set_param(std::string_view name, const glm::vec4& value) {
        if (!material_override) {
            material_override.emplace();
        }
        material_override->set_vec4(name, value);
    }

    // Removes every per-renderable override (uniforms and albedo).
    void clear_overrides() { material_override.reset(); }

    [[nodiscard]] glm::vec4 tinted_color() const {
        const glm::vec4 material_color =
                material ? material->color() : glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
        return multiply_instance_color(material_color, color);
    }
};

struct RenderableItem {
    Renderable renderable;
    ecs::Entity entity;
    glm::vec3 world_position{0.0f, 0.0f, 0.0f};
};

[[nodiscard]] inline DrawSortKey draw_sort_key(const RenderableItem& item, const glm::mat4& view) {
    return make_draw_sort_key(item.renderable.material.get(), item.renderable.layer, item.renderable.order_in_layer,
            item.world_position, view, item.entity);
}

// Stable sort by `draw_sort_less`, the key `run_render` uses. `view` is the camera's view matrix.
inline void sort_renderables(std::span<RenderableItem> items, const glm::mat4& view) {
    std::stable_sort(items.begin(), items.end(), [&view](const RenderableItem& a, const RenderableItem& b) {
        return draw_sort_less(draw_sort_key(a, view), draw_sort_key(b, view));
    });
}

}
