#include "ui/render/render_list.h"

namespace engine::ui {

void RenderList::clear() noexcept {
    vertices_.clear();
    indices_.clear();
    batches_.clear();
}

void RenderList::bind(UiTextureSlot slot, std::uint32_t page) {
    const auto index = static_cast<std::size_t>(slot);
    if (!batches_.empty()) {
        UiBatch& current = batches_.back();
        if (current.pages[index] == page) {
            return;
        }
        if (current.pages[index] == UiBatch::kUnbound || current.index_count == 0) {
            current.pages[index] = page;
            return;
        }
    }
    UiBatch next;
    next.index_offset = static_cast<std::uint32_t>(indices_.size());
    next.pages[index] = page;
    batches_.push_back(next);
}

UiVertex* RenderList::push_quad() {
    if (batches_.empty()) {
        batches_.push_back(UiBatch{});
    }
    const auto base = static_cast<UiIndex>(vertices_.size());
    vertices_.resize(vertices_.size() + 4);
    indices_.insert(indices_.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    batches_.back().index_count += 6;
    return vertices_.data() + base;
}

}
