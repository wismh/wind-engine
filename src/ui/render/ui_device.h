#pragma once

// THE interface a UI backend implements. docs/tech/architecture/UI Performance Plan.md, step 5 (Design, wind-234).
//
// A new backend (Vulkan, Metal, D3D) writes one class against this header and one shader that implements fragment.h
// line for line, with the pipeline of ui_pipeline.h (vertex layout, premultiplied blend, four sampler slots). Nothing
// else: fonts, atlases, batching, and texture catch-up (UiTextureSync) are backend-neutral and already done by the
// time a call reaches the device. The OpenGL device is src/render/opengl/opengl_ui_device.{h,cpp}.
//
// A frame, as UiRenderer::end_frame drives it:
//
//   begin_frame(width, height, pixel_ratio)
//   create_texture / update_texture ...      only for pages that are new or changed since this device last saw them
//   upload(vertices, indices)
//   draw(batches)
//   end_frame()
//
// Every call of a frame comes between begin_frame and end_frame; the device may set any state it needs in begin_frame
// and must give the rest of the renderer back what it expects in end_frame. release_textures may also come outside a
// frame. The device runs on the main thread, with its target (GL: its context) current.

#include "ui/render/atlas_page.h"
#include "ui/render/render_list.h"
#include "ui/render/ui_pipeline.h"
#include "ui/render/ui_vertex.h"

#include <cstdint>
#include <span>

namespace engine::ui {

enum class UiTextureFormat : std::uint8_t {
    // One byte per texel, sampled as coverage in `.r` (Glyph, Mask).
    R8,
    // Four bytes per texel, RGBA, red in the lowest address (Ramp premultiplied, Image straight).
    Rgba8,
};

// Texels for a texture or a region of one. `pixels` is the whole source page, row-major, `row_texels` texels per row
// (the page's width); `rect` is the region to copy, in texels of that page, which is also where it lands in the
// texture (a page and its texture are the same size).
struct UiTextureRegion {
    std::span<const std::uint8_t> pixels;
    int row_texels = 0;
    AtlasRect rect;
};

class IUiDevice {
public:
    virtual ~IUiDevice() = default;

    // Starts a frame drawn over a `width` x `height` target in logical units (UiVertex::position; origin top left,
    // y down), `pixel_ratio` device pixels per unit. Resets draw_calls().
    virtual void begin_frame(float width, float height, float pixel_ratio) = 0;
    // (Re)creates the texture of `page` in `slot`: `width` x `height`, filled from `texels` (whose rect is the whole
    // texture). Bilinear, no mipmaps, clamp to edge. An existing texture there is replaced.
    virtual void create_texture(UiTextureSlot slot, std::uint32_t page, int width, int height, UiTextureFormat format,
            const UiTextureRegion& texels) = 0;
    // Overwrites `texels.rect` of the texture of `page` in `slot` (created earlier, same format).
    virtual void update_texture(UiTextureSlot slot, std::uint32_t page, const UiTextureRegion& texels) = 0;
    // Drops every texture (PaintResources::generation changed, or the device is going away).
    virtual void release_textures() = 0;
    // This frame's geometry (RenderList::vertices / indices). Replaces the previous upload; the spans may be read
    // until the call returns only.
    virtual void upload(std::span<const UiVertex> vertices, std::span<const UiIndex> indices) = 0;
    // Draws `batches` in order, each its index range of the last upload with its pages bound (UiBatch::pages, one
    // texture per slot; kUnbound leaves the slot as it is). An empty batch draws nothing.
    virtual void draw(std::span<const UiBatch> batches) = 0;
    // Ends the frame: restores the state the rest of the backend expects.
    virtual void end_frame() = 0;
    // Draw calls issued since begin_frame.
    [[nodiscard]] virtual int draw_calls() const = 0;
};

}
