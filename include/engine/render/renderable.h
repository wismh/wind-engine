#pragma once

#include <engine/ecs/entity.h>
#include <engine/render/graphics.h>
#include <engine/render/material.h>

#include <glm/vec4.hpp>

#include <algorithm>
#include <functional>
#include <memory>
#include <span>

namespace engine::render {

struct Renderable {
    std::shared_ptr<IMesh> mesh;
    std::shared_ptr<IMaterial> material;
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    int layer = 0;
    int order_in_layer = 0;

    [[nodiscard]] glm::vec4 tinted_color() const {
        const glm::vec4 material_color =
                material ? material->Color() : glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
        return multiply_instance_color(material_color, color);
    }
};

struct RenderableItem {
    Renderable renderable;
    ecs::Entity entity;
};

inline bool renderable_less(const RenderableItem& a, const RenderableItem& b) {
    if (a.renderable.layer != b.renderable.layer) {
        return a.renderable.layer < b.renderable.layer;
    }
    if (a.renderable.order_in_layer != b.renderable.order_in_layer) {
        return a.renderable.order_in_layer < b.renderable.order_in_layer;
    }
    const IMaterial* const mat_a = a.renderable.material.get();
    const IMaterial* const mat_b = b.renderable.material.get();
    if (mat_a != mat_b) {
        return std::less<>{}(mat_a, mat_b);
    }
    return a.entity.index < b.entity.index;
}

inline void sort_renderables(std::span<RenderableItem> items) {
    std::stable_sort(items.begin(), items.end(), renderable_less);
}

}
