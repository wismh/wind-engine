#pragma once

#include "ui/render/ui_pipeline.h"
#include "ui/render/ui_vertex.h"

#include <array>
#include <cstdint>
#include <vector>

namespace engine::ui {

// One draw call: a run of indices drawn with one set of bound pages. `pages[slot]` is a page index of that slot's
// atlas (UiTextureSlot), or kUnbound when nothing in the batch samples the slot (the device may leave whatever is
// bound there).
struct UiBatch {
    static constexpr std::uint32_t kUnbound = 0xFFFFFFFFu;

    std::array<std::uint32_t, kUiTextureSlotCount> pages{kUnbound, kUnbound, kUnbound, kUnbound};
    std::uint32_t index_offset = 0;
    std::uint32_t index_count = 0;
};

// One frame's UI geometry for one target, in draw order: vertices, indices, and the batches that split them. Filled
// by BatchingPainter, consumed by a device. `clear()` keeps the capacity, so a frame no larger than the last one
// allocates nothing.
class RenderList {
public:
    void clear() noexcept;

    // Makes `page` of `slot` bound for the next quad. Starts a new batch only when the current one already binds a
    // different page there; a slot the current batch never used is bound in place.
    void bind(UiTextureSlot slot, std::uint32_t page);
    // Appends a quad to the current batch (starting the first one if needed) and returns its 4 vertices, to be filled
    // in order around the quad (the indices are 0 1 2, 0 2 3). Valid until the next push.
    [[nodiscard]] UiVertex* push_quad();

    [[nodiscard]] const std::vector<UiVertex>& vertices() const noexcept { return vertices_; }
    [[nodiscard]] const std::vector<UiIndex>& indices() const noexcept { return indices_; }
    [[nodiscard]] const std::vector<UiBatch>& batches() const noexcept { return batches_; }
    [[nodiscard]] std::size_t quad_count() const noexcept { return vertices_.size() / 4; }

private:
    std::vector<UiVertex> vertices_;
    std::vector<UiIndex> indices_;
    std::vector<UiBatch> batches_;
};

}
