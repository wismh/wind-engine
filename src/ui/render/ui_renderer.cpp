#include "ui/render/ui_renderer.h"

#include <engine/builtin_ids.h>

namespace engine::ui {

UiRenderer::UiRenderer(PaintResources& resources, IUiDevice& device) : painter_(resources), device_(device) {}

bool UiRenderer::load_ui_font(const Font& font) {
    return add_font(builtin::font_ui, font);
}

bool UiRenderer::add_font(AssetId id, const Font& font) {
    return painter_.resources().fonts.add_font(id, font);
}

bool UiRenderer::add_image(AssetId id, const render::TextureDesc& desc) {
    return painter_.resources().images.add(id, desc);
}

void UiRenderer::drop_images() {
    painter_.resources().drop_images();
}

void UiRenderer::begin_frame(float width, float height, float pixel_ratio) {
    painter_.begin_frame(width, height, pixel_ratio);
}

void UiRenderer::end_frame() {
    painter_.end_frame();
    const RenderList& list = painter_.list();
    // A minimized window's target is empty: nothing to draw into.
    if (list.indices().empty() || painter_.width() <= 0.0f || painter_.height() <= 0.0f) {
        return;
    }
    device_.begin_frame(painter_.width(), painter_.height(), painter_.pixel_ratio());
    textures_.sync(painter_.resources(), device_);
    device_.upload(list.vertices(), list.indices());
    device_.draw(list.batches());
    device_.end_frame();
}

}
