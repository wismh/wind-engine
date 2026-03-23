#include "ui/render/ui_texture_sync.h"

#include <cstddef>

namespace engine::ui {
namespace {

template <class Atlas>
void sync_slot(const Atlas& atlas, UiTextureSlot slot, std::vector<std::uint64_t>& uploaded, IUiDevice& device) {
    for (std::size_t i = 0; i < atlas.page_count(); ++i) {
        const AtlasPage& page = atlas.page(i);
        const auto index = static_cast<std::uint32_t>(i);
        if (i >= uploaded.size()) {
            const UiTextureRegion whole{page.pixels(), page.width(), {0, 0, page.width(), page.height()}};
            device.create_texture(slot, index, page.width(), page.height(),
                    page.channels() == 1 ? UiTextureFormat::R8 : UiTextureFormat::Rgba8, whole);
            uploaded.push_back(page.version());
            continue;
        }
        if (uploaded[i] == page.version()) {
            continue;
        }
        if (const AtlasRect dirty = page.dirty_since(uploaded[i]); !dirty.empty()) {
            device.update_texture(slot, index, UiTextureRegion{page.pixels(), page.width(), dirty});
        }
        uploaded[i] = page.version();
    }
}

}

void UiTextureSync::sync(const PaintResources& resources, IUiDevice& device) {
    if (generation_ != resources.generation) {
        if (generation_ != kNoGeneration) {
            device.release_textures();
        }
        forget();
        generation_ = resources.generation;
    }
    sync_slot(resources.fonts.atlas(), UiTextureSlot::Glyph, uploaded_[static_cast<std::size_t>(UiTextureSlot::Glyph)],
            device);
    sync_slot(resources.masks, UiTextureSlot::Mask, uploaded_[static_cast<std::size_t>(UiTextureSlot::Mask)], device);
    sync_slot(resources.ramps, UiTextureSlot::Ramp, uploaded_[static_cast<std::size_t>(UiTextureSlot::Ramp)], device);
    sync_slot(resources.images, UiTextureSlot::Image, uploaded_[static_cast<std::size_t>(UiTextureSlot::Image)],
            device);
}

void UiTextureSync::forget() noexcept {
    for (std::vector<std::uint64_t>& slot : uploaded_) {
        slot.clear();
    }
    generation_ = kNoGeneration;
}

}
