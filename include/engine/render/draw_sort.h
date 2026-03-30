#pragma once

// docs/tech/features/Materials and Sort.md

#include <engine/ecs/entity.h>
#include <engine/render/material.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <functional>

namespace engine::render {

// Opaque draws all go before transparent ones, whatever their layer.
enum class DrawQueue {
    Opaque,
    Transparent,
};

// Opaque is an opaque blend that writes depth: the depth buffer orders it, so layer and order do not. Every other
// state (alpha, additive, or opaque without depth write, which is every 2D material) is transparent.
[[nodiscard]] constexpr DrawQueue draw_queue(const RenderState& state) {
    return state.blend == BlendMode::Opaque && state.depth_write ? DrawQueue::Opaque : DrawQueue::Transparent;
}

// One world draw's sort key; `draw_sort_less` compares it. `run_render` builds one per command.
struct DrawSortKey {
    DrawQueue queue = DrawQueue::Transparent;
    int layer = 0;
    int order_in_layer = 0;
    // True only for a draw that is depth tested. Only those compare by `view_depth` in the transparent queue, so a
    // 2D scene (no depth-tested material) keeps layer, order, material, entity whatever the z of its sprites.
    bool depth_sorted = false;
    // Distance in front of the camera: minus the view-space z of the draw's world position.
    float view_depth = 0.0f;
    const IMaterial* material = nullptr;
    std::uint32_t entity_index = 0;
};

// `world_position` is the draw's origin in world space (the translation of its model matrix). `depth_tested` is
// false for a draw the backend runs without depth test whatever the material says (particles).
[[nodiscard]] inline DrawSortKey make_draw_sort_key(const IMaterial* material, int layer, int order_in_layer,
        const glm::vec3& world_position, const glm::mat4& view, ecs::Entity entity, bool depth_tested = true) {
    const RenderState state = material != nullptr ? material->render_state() : RenderState{};
    const bool sorted = depth_tested && state.depth_test;
    return DrawSortKey{
            .queue = depth_tested ? draw_queue(state) : DrawQueue::Transparent,
            .layer = layer,
            .order_in_layer = order_in_layer,
            .depth_sorted = sorted,
            .view_depth = -(view * glm::vec4(world_position, 1.0f)).z,
            .material = material,
            .entity_index = entity.index,
    };
}

// Opaque queue: material (state batching), front to back, entity index.
// Transparent queue: layer, order_in_layer, depth-sorted draws before the rest, back to front among depth-sorted
// draws, material, entity index.
[[nodiscard]] inline bool draw_sort_less(const DrawSortKey& a, const DrawSortKey& b) {
    if (a.queue != b.queue) {
        return a.queue < b.queue;
    }
    if (a.queue == DrawQueue::Opaque) {
        if (a.material != b.material) {
            return std::less<>{}(a.material, b.material);
        }
        if (a.view_depth != b.view_depth) {
            return a.view_depth < b.view_depth;
        }
        return a.entity_index < b.entity_index;
    }
    if (a.layer != b.layer) {
        return a.layer < b.layer;
    }
    if (a.order_in_layer != b.order_in_layer) {
        return a.order_in_layer < b.order_in_layer;
    }
    if (a.depth_sorted != b.depth_sorted) {
        return a.depth_sorted;
    }
    if (a.depth_sorted && a.view_depth != b.view_depth) {
        return a.view_depth > b.view_depth;
    }
    if (a.material != b.material) {
        return std::less<>{}(a.material, b.material);
    }
    return a.entity_index < b.entity_index;
}

}
