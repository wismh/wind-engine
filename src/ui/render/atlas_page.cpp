#include "ui/render/atlas_page.h"

#include <algorithm>

namespace engine::ui {

AtlasPage::AtlasPage(int width, int height, int channels) :
    packer_(width, height), channels_(channels),
    pixels_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(channels),
            0) {}

void AtlasPage::reset_packing() {
    packer_ = SkylinePacker(packer_.width(), packer_.height());
}

AtlasRect AtlasPage::dirty_since(std::uint64_t version) const {
    // Writes are in version order: the ones after `version` are a suffix.
    const auto first = std::upper_bound(writes_.begin(), writes_.end(), version,
            [](std::uint64_t v, const Write& write) { return v < write.version; });
    AtlasRect dirty{width(), height(), 0, 0};
    for (auto it = first; it != writes_.end(); ++it) {
        dirty.x0 = std::min(dirty.x0, it->rect.x0);
        dirty.y0 = std::min(dirty.y0, it->rect.y0);
        dirty.x1 = std::max(dirty.x1, it->rect.x1);
        dirty.y1 = std::max(dirty.y1, it->rect.y1);
    }
    return dirty.empty() ? AtlasRect{} : dirty;
}

void AtlasPage::mark_dirty(const AtlasRect& rect, std::uint64_t version) {
    version_ = version;
    writes_.push_back({version, rect});
}

}
