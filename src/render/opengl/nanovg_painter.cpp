#include "nanovg_painter.h"

#include "gl_includes.h"

#if defined(ENGINE_WITH_GLES)
#define NANOVG_GLES3_IMPLEMENTATION
#else
#define NANOVG_GL3_IMPLEMENTATION
#endif
#include <nanovg.h>
#include <nanovg_gl.h>

#include <engine/builtin_ids.h>
#include <engine/resources/font.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::render {
namespace {

NVGcolor to_nvg(glm::vec4 color) {
    return nvgRGBAf(color.r, color.g, color.b, color.a);
}

}

struct ImageEntry {
    int nvg_id = -1;
    int width = 0;
    int height = 0;
};

struct NanoVgPainter::Impl {
    NVGcontext* vg = nullptr;
    std::vector<std::vector<std::uint8_t>> font_blobs;
    std::unordered_map<std::string, int> fonts;
    std::unordered_map<std::string, ImageEntry> images;
    int default_font = -1;
};

NanoVgPainter::NanoVgPainter() : impl_(std::make_unique<Impl>()) {}

NanoVgPainter::~NanoVgPainter() {
    destroy();
}

bool NanoVgPainter::create() {
    destroy();
    impl_->vg =
#if defined(ENGINE_WITH_GLES)
            nvgCreateGLES3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
#else
            nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
#endif
    return impl_->vg != nullptr;
}

bool NanoVgPainter::add_font(AssetId id, const Font& font) {
    if (impl_->vg == nullptr || font.bytes.empty()) {
        return false;
    }
    const std::string key(id.hex());
    if (impl_->fonts.contains(key)) {
        return true;
    }
    impl_->font_blobs.push_back(font.bytes);
    auto& blob = impl_->font_blobs.back();
    const int nvg_id = nvgCreateFontMem(impl_->vg, key.c_str(), blob.data(), static_cast<int>(blob.size()), 0);
    if (nvg_id < 0) {
        impl_->font_blobs.pop_back();
        return false;
    }
    impl_->fonts.emplace(key, nvg_id);
    return true;
}

bool NanoVgPainter::load_ui_font(const Font& font) {
    if (!add_font(builtin::font_ui, font)) {
        return false;
    }
    impl_->default_font = impl_->fonts[std::string(builtin::font_ui.hex())];
    return true;
}

bool NanoVgPainter::add_image(AssetId id, const TextureDesc& desc) {
    if (impl_->vg == nullptr || desc.width <= 0 || desc.height <= 0 || desc.rgba.empty()) {
        return false;
    }
    const std::string key(id.hex());
    if (impl_->images.contains(key)) {
        return true;
    }
    // Every image is created wrapping (REPEATX/REPEATY) rather than clamping. This is a no-op for
    // every non-tiled draw: image()/image_nine_slice() always size the nvgImagePattern extent to
    // exactly match the filled rect, so texture coordinates never leave [0,1] and the GL wrap mode
    // never becomes visible. image_repeat() is the only caller that relies on it, by making the
    // pattern extent smaller than the filled rect so sampling wraps into repeated tiles.
    const int nvg_id = nvgCreateImageRGBA(
            impl_->vg, desc.width, desc.height, NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY, desc.rgba.data());
    if (nvg_id <= 0) {
        return false;
    }
    impl_->images.emplace(key, ImageEntry{nvg_id, desc.width, desc.height});
    return true;
}

void NanoVgPainter::destroy() {
    if (impl_ == nullptr) {
        return;
    }
    if (impl_->vg != nullptr) {
#if defined(ENGINE_WITH_GLES)
        nvgDeleteGLES3(impl_->vg);
#else
        nvgDeleteGL3(impl_->vg);
#endif
        impl_->vg = nullptr;
    }
    impl_->fonts.clear();
    impl_->images.clear();
    impl_->default_font = -1;
    impl_->font_blobs.clear();
}

void NanoVgPainter::begin_frame(float width, float height, float pixel_ratio) {
    if (impl_->vg != nullptr) {
        nvgBeginFrame(impl_->vg, width, height, pixel_ratio);
    }
}

void NanoVgPainter::end_frame() {
    if (impl_->vg != nullptr) {
        nvgEndFrame(impl_->vg);
    }
}

void NanoVgPainter::save() {
    if (impl_->vg != nullptr) {
        nvgSave(impl_->vg);
    }
}

void NanoVgPainter::restore() {
    if (impl_->vg != nullptr) {
        nvgRestore(impl_->vg);
    }
}

void NanoVgPainter::apply_transform(glm::vec2 center, float rotation_radians, float scale) {
    if (impl_->vg == nullptr) {
        return;
    }
    nvgTranslate(impl_->vg, center.x, center.y);
    nvgRotate(impl_->vg, rotation_radians);
    nvgScale(impl_->vg, scale, scale);
    nvgTranslate(impl_->vg, -center.x, -center.y);
}

void NanoVgPainter::scissor(const Rect& rect) {
    if (impl_->vg != nullptr) {
        nvgIntersectScissor(impl_->vg, rect.x, rect.y, rect.w, rect.h);
    }
}

void NanoVgPainter::apply_view(glm::vec2 origin, glm::vec2 pan, float zoom) {
    if (impl_->vg == nullptr) {
        return;
    }
    // Same T(origin) S T(pan) T(-origin) order as apply_transform's scale-about-center: pan is in
    // pre-scale units so a layout point L maps to origin + zoom * (L - origin + pan). Translating
    // by zoom*pan after apply_transform() would apply pan before the scale-about-origin and paint
    // Z²P, so wheel zoom-to-cursor would drift off the pointer.
    nvgTranslate(impl_->vg, origin.x, origin.y);
    nvgScale(impl_->vg, zoom, zoom);
    nvgTranslate(impl_->vg, pan.x, pan.y);
    nvgTranslate(impl_->vg, -origin.x, -origin.y);
}

void NanoVgPainter::set_opacity(float opacity) {
    if (impl_->vg != nullptr) {
        nvgGlobalAlpha(impl_->vg, std::clamp(opacity, 0.0f, 1.0f));
    }
}

void NanoVgPainter::fill_rounded_rect(const Rect& rect, float radius, glm::vec4 color) {
    if (impl_->vg == nullptr) {
        return;
    }
    nvgBeginPath(impl_->vg);
    nvgRoundedRect(impl_->vg, rect.x, rect.y, rect.w, rect.h, radius);
    nvgFillColor(impl_->vg, to_nvg(color));
    nvgFill(impl_->vg);
}

void NanoVgPainter::stroke_rounded_rect(const Rect& rect, float radius, float width, glm::vec4 color) {
    if (impl_->vg == nullptr) {
        return;
    }
    nvgBeginPath(impl_->vg);
    nvgRoundedRect(impl_->vg, rect.x, rect.y, rect.w, rect.h, radius);
    nvgStrokeWidth(impl_->vg, width);
    nvgStrokeColor(impl_->vg, to_nvg(color));
    nvgStroke(impl_->vg);
}

void NanoVgPainter::draw_line(glm::vec2 from, glm::vec2 to, glm::vec4 color, float width) {
    if (impl_->vg == nullptr) {
        return;
    }
    nvgBeginPath(impl_->vg);
    nvgMoveTo(impl_->vg, from.x, from.y);
    nvgLineTo(impl_->vg, to.x, to.y);
    nvgStrokeWidth(impl_->vg, width);
    nvgStrokeColor(impl_->vg, to_nvg(color));
    nvgStroke(impl_->vg);
}

void NanoVgPainter::set_font(AssetId font, float size) {
    if (impl_->vg == nullptr) {
        return;
    }
    nvgFontSize(impl_->vg, size);
    int id = impl_->default_font;
    const auto it = impl_->fonts.find(std::string(font.hex()));
    if (it != impl_->fonts.end()) {
        id = it->second;
    }
    if (id >= 0) {
        nvgFontFaceId(impl_->vg, id);
    }
}

void NanoVgPainter::fill_text(std::string_view text, glm::vec2 position, glm::vec4 color, ui::UiAlign horizontal,
        ui::UiAlign vertical) {
    if (impl_->vg == nullptr || text.empty()) {
        return;
    }
    int align = 0;
    switch (horizontal) {
        case ui::UiAlign::Center:
            align |= NVG_ALIGN_CENTER;
            break;
        case ui::UiAlign::End:
            align |= NVG_ALIGN_RIGHT;
            break;
        case ui::UiAlign::Start:
        default:
            align |= NVG_ALIGN_LEFT;
            break;
    }
    switch (vertical) {
        case ui::UiAlign::Center:
            align |= NVG_ALIGN_MIDDLE;
            break;
        case ui::UiAlign::End:
            align |= NVG_ALIGN_BOTTOM;
            break;
        case ui::UiAlign::Start:
        default:
            align |= NVG_ALIGN_TOP;
            break;
    }
    const std::string z(text);
    nvgFillColor(impl_->vg, to_nvg(color));
    nvgTextAlign(impl_->vg, align);
    nvgText(impl_->vg, position.x, position.y, z.c_str(), nullptr);
}

void NanoVgPainter::image(AssetId texture, const Rect& rect) {
    if (impl_->vg == nullptr) {
        return;
    }
    const auto it = impl_->images.find(std::string(texture.hex()));
    if (it == impl_->images.end()) {
        return;
    }
    const NVGpaint paint =
            nvgImagePattern(impl_->vg, rect.x, rect.y, rect.w, rect.h, 0.0f, it->second.nvg_id, 1.0f);
    nvgBeginPath(impl_->vg);
    nvgRect(impl_->vg, rect.x, rect.y, rect.w, rect.h);
    nvgFillPaint(impl_->vg, paint);
    nvgFill(impl_->vg);
}

void NanoVgPainter::image_repeat(AssetId texture, const Rect& rect) {
    if (impl_->vg == nullptr) {
        return;
    }
    const auto it = impl_->images.find(std::string(texture.hex()));
    if (it == impl_->images.end() || it->second.width <= 0 || it->second.height <= 0) {
        return;
    }
    // Pattern extent = the texture's own pixel size (not `rect`), anchored at rect's origin, so
    // filling `rect` (almost always larger) samples past [0,1] and wraps into repeated tiles.
    const NVGpaint paint = nvgImagePattern(impl_->vg, rect.x, rect.y,
            static_cast<float>(it->second.width), static_cast<float>(it->second.height), 0.0f, it->second.nvg_id,
            1.0f);
    nvgBeginPath(impl_->vg);
    nvgRect(impl_->vg, rect.x, rect.y, rect.w, rect.h);
    nvgFillPaint(impl_->vg, paint);
    nvgFill(impl_->vg);
}

void NanoVgPainter::image_nine_slice(AssetId texture, const Rect& rect, const ui::BoxInsets& insets) {
    if (impl_->vg == nullptr || rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    const auto it = impl_->images.find(std::string(texture.hex()));
    if (it == impl_->images.end()) {
        return;
    }
    const int nvg_id = it->second.nvg_id;
    const float tw = static_cast<float>(it->second.width);
    const float th = static_cast<float>(it->second.height);
    if (tw <= 0.0f || th <= 0.0f) {
        return;
    }

    float sl = std::max(0.0f, insets.left);
    float sr = std::max(0.0f, insets.right);
    float st = std::max(0.0f, insets.top);
    float sb = std::max(0.0f, insets.bottom);

    if (sl + sr > tw) {
        const float s = tw / (sl + sr);
        sl *= s;
        sr *= s;
    }
    if (st + sb > th) {
        const float s = th / (st + sb);
        st *= s;
        sb *= s;
    }

    float dl = sl;
    float dr = sr;
    float dt = st;
    float db = sb;

    if (dl + dr > rect.w) {
        const float s = rect.w / (dl + dr);
        dl *= s;
        dr *= s;
    }
    if (dt + db > rect.h) {
        const float s = rect.h / (dt + db);
        dt *= s;
        db *= s;
    }

    const float dw_center = std::max(0.0f, rect.w - dl - dr);
    const float dh_center = std::max(0.0f, rect.h - dt - db);
    const float sw_center = std::max(0.0f, tw - sl - sr);
    const float sh_center = std::max(0.0f, th - st - sb);

    const float src_x[3] = {0.0f, sl, tw - sr};
    const float src_w[3] = {sl, sw_center, sr};
    const float dst_x[3] = {rect.x, rect.x + dl, rect.x + rect.w - dr};
    const float dst_w[3] = {dl, dw_center, dr};

    const float src_y[3] = {0.0f, st, th - sb};
    const float src_h[3] = {st, sh_center, sb};
    const float dst_y[3] = {rect.y, rect.y + dt, rect.y + rect.h - db};
    const float dst_h[3] = {dt, dh_center, db};

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const float sw = src_w[col];
            const float sh = src_h[row];
            const float dw = dst_w[col];
            const float dh = dst_h[row];
            if (dw <= 0.0f || dh <= 0.0f || sw <= 0.0f || sh <= 0.0f) {
                continue;
            }
            const float sx = src_x[col];
            const float sy = src_y[row];
            const float dx = dst_x[col];
            const float dy = dst_y[row];

            const float scale_x = dw / sw;
            const float scale_y = dh / sh;
            const float ox = dx - sx * scale_x;
            const float oy = dy - sy * scale_y;
            const float ex = tw * scale_x;
            const float ey = th * scale_y;

            const NVGpaint paint = nvgImagePattern(impl_->vg, ox, oy, ex, ey, 0.0f, nvg_id, 1.0f);
            nvgBeginPath(impl_->vg);
            nvgRect(impl_->vg, dx, dy, dw, dh);
            nvgFillPaint(impl_->vg, paint);
            nvgFill(impl_->vg);
        }
    }
}

glm::vec2 NanoVgPainter::measure_text(std::string_view text, AssetId font, float size) {
    if (impl_->vg == nullptr) {
        return {static_cast<float>(text.size()) * size * 0.5f, size};
    }
    nvgSave(impl_->vg);
    set_font(font, size);
    nvgTextAlign(impl_->vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    const std::string z(text);
    float bounds[4] = {};
    nvgTextBounds(impl_->vg, 0.0f, 0.0f, z.c_str(), nullptr, bounds);
    nvgRestore(impl_->vg);
    return {std::max(0.0f, bounds[2] - bounds[0]), std::max(0.0f, bounds[3] - bounds[1])};
}

}
