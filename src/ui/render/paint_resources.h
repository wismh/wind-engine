#pragma once

#include "ui/render/font_library.h"
#include "ui/render/image_atlas.h"
#include "ui/render/mask_atlas.h"
#include "ui/render/ramp_atlas.h"

#include <cstdint>

namespace engine::ui {

// The CPU state every UI draw list draws from, shared by every window: fonts and the glyph atlas, path masks,
// gradient ramps, and images. No GPU handle inside; each device keeps its own textures per page and catches up with
// the page versions (AtlasPage::dirty_since), like the per-context VAOs of wind-233. Main thread only.
//
// The pages a batch binds (UiBatch::pages) index these atlases: Glyph -> fonts.atlas(), Mask -> masks, Ramp -> ramps,
// Image -> images. Mask and ramp entries are evicted only when no list since the last begin_list used them, so a
// draw list must reach its device before another list over the same resources begins.
//
// `generation` changes when page indices stop meaning what they meant (drop_images): a device then drops every texture
// it made from these resources and uploads the pages again from scratch (UiTextureSync).
struct PaintResources {
    explicit PaintResources(int glyph_page_size = GlyphAtlas::kDefaultPageSize,
            int mask_page_size = MaskAtlas::kDefaultPageSize, int image_page_size = ImageAtlas::kDefaultPageSize) :
        fonts(glyph_page_size), masks(mask_page_size), images(image_page_size) {}

    FontLibrary fonts;
    MaskAtlas masks;
    RampAtlas ramps;
    ImageAtlas images;

    std::uint64_t generation = 0;

    void begin_list() noexcept {
        masks.begin_list();
        ramps.begin_list();
    }

    // Forgets every registered image (EngineRuntime::reset_ui_cache: the images of the last game build must not
    // outlive it; the next frame registers what its canvases reference again). Fonts stay: every window shares them,
    // and a window that measures text before it registers its fonts again would measure without them. So a font
    // changed under the same AssetId keeps its first bytes. Path masks and gradient ramps are keyed by content.
    void drop_images() noexcept {
        images.clear();
        ++generation;
    }
};

}
