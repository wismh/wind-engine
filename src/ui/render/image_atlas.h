#pragma once

#include "ui/render/atlas_page.h"

#include <engine/render/graphic_factory.h>
#include <engine/resources/asset_id.h>

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::ui {

// A registered image: its page and its texel rect there, normalized (the gutter excluded).
struct AtlasImage {
    std::uint32_t page = 0;
    glm::vec2 uv_min{};
    glm::vec2 uv_size{};
    int width = 0;
    int height = 0;
};

// RGBA8 straight-alpha pages of UI images, registered by AssetId from a TextureDesc and kept for the atlas's life (as
// the NanoVG painter kept its images). Each image is packed with a one-texel gutter holding the texels of the opposite
// edge: the NanoVG painter created every image with REPEATX | REPEATY, so a bilinear fetch at an image's edge blended
// in the far edge; the gutter keeps that, and makes ImageRepeat's fract seamless. An image too large for a page gets a
// page of its own size. TextureDesc's filter and wrap are ignored, as the NanoVG painter ignored them (bilinear,
// repeat). CPU only; a device catches up with page versions. Main thread only.
class ImageAtlas {
public:
    static constexpr int kDefaultPageSize = 1024;
    static constexpr int kGutter = 1;

    explicit ImageAtlas(int page_size = kDefaultPageSize);

    // False when `desc` is empty or its pixels are short; true when `id` is already registered.
    bool add(AssetId id, const render::TextureDesc& desc);
    // Forgets every image and page. The version keeps counting, but a device must not catch up across a clear by
    // version alone (a page index may now be another size): PaintResources::drop_images bumps its generation.
    void clear() noexcept;
    [[nodiscard]] const AtlasImage* find(AssetId id) const;

    [[nodiscard]] std::size_t page_count() const noexcept { return pages_.size(); }
    [[nodiscard]] const AtlasPage& page(std::size_t index) const { return pages_[index]; }
    [[nodiscard]] std::uint64_t version() const noexcept { return version_; }
    [[nodiscard]] std::size_t image_count() const noexcept { return images_.size(); }

private:
    struct IdHash {
        std::size_t operator()(const AssetId& id) const noexcept { return std::hash<std::string_view>{}(id.hex()); }
    };

    int page_size_ = kDefaultPageSize;
    std::vector<AtlasPage> pages_;
    // Pages images are packed into (a page made for one large image is not).
    std::vector<bool> shared_;
    std::uint64_t version_ = 0;
    std::unordered_map<AssetId, AtlasImage, IdHash> images_;
};

}
