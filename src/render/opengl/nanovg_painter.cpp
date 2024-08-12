#include "nanovg_painter.h"

#include "gl_includes.h"

#define NANOVG_GL3_IMPLEMENTATION
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

struct NanoVgPainter::Impl {
    NVGcontext* vg = nullptr;
    std::vector<std::vector<std::uint8_t>> font_blobs;
    std::unordered_map<std::string, int> fonts;
    std::unordered_map<std::string, int> images;
    int default_font = -1;
};

NanoVgPainter::NanoVgPainter() : impl_(std::make_unique<Impl>()) {}

NanoVgPainter::~NanoVgPainter() {
    destroy();
}

bool NanoVgPainter::create() {
    destroy();
    impl_->vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
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
    const int nvg_id =
            nvgCreateImageRGBA(impl_->vg, desc.width, desc.height, 0, desc.rgba.data());
    if (nvg_id <= 0) {
        return false;
    }
    impl_->images.emplace(key, nvg_id);
    return true;
}

void NanoVgPainter::destroy() {
    if (impl_ == nullptr) {
        return;
    }
    if (impl_->vg != nullptr) {
        nvgDeleteGL3(impl_->vg);
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

void NanoVgPainter::scissor(const Rect& rect) {
    if (impl_->vg != nullptr) {
        nvgScissor(impl_->vg, rect.x, rect.y, rect.w, rect.h);
    }
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
            nvgImagePattern(impl_->vg, rect.x, rect.y, rect.w, rect.h, 0.0f, it->second, 1.0f);
    nvgBeginPath(impl_->vg);
    nvgRect(impl_->vg, rect.x, rect.y, rect.w, rect.h);
    nvgFillPaint(impl_->vg, paint);
    nvgFill(impl_->vg);
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
