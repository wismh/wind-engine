#include "nanovg_painter.h"

#include "gl_includes.h"

#define NANOVG_GL3_IMPLEMENTATION
#include <nanovg.h>
#include <nanovg_gl.h>

#include <engine/builtin_ids.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::render {
namespace {

NVGcolor to_nvg(glm::vec4 color) {
    return nvgRGBAf(color.r, color.g, color.b, color.a);
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}

struct NanoVgPainter::Impl {
    NVGcontext* vg = nullptr;
    std::vector<std::uint8_t> font_bytes;
    std::unordered_map<std::string, int> fonts;
    int default_font = -1;
};

NanoVgPainter::NanoVgPainter() : impl_(std::make_unique<Impl>()) {}

NanoVgPainter::~NanoVgPainter() {
    destroy();
}

bool NanoVgPainter::create() {
    destroy();
    impl_->vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (impl_->vg == nullptr) {
        return false;
    }

    std::filesystem::path font_path;
    if (const char* base = SDL_GetBasePath(); base != nullptr) {
        font_path = std::filesystem::path(base) / "assets" / "engine" / "fonts" / "ui.ttf";
    }
    impl_->font_bytes = read_file(font_path);
    if (!impl_->font_bytes.empty()) {
        impl_->default_font = nvgCreateFontMem(impl_->vg, "default", impl_->font_bytes.data(),
                static_cast<int>(impl_->font_bytes.size()), 0);
        if (impl_->default_font >= 0) {
            impl_->fonts.emplace(std::string(builtin::font_ui.hex()), impl_->default_font);
        }
    }
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
    impl_->default_font = -1;
    impl_->font_bytes.clear();
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

void NanoVgPainter::fill_text(std::string_view text, glm::vec2 position, glm::vec4 color) {
    if (impl_->vg == nullptr || text.empty()) {
        return;
    }
    const std::string z(text);
    nvgFillColor(impl_->vg, to_nvg(color));
    nvgTextAlign(impl_->vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgText(impl_->vg, position.x, position.y, z.c_str(), nullptr);
}

void NanoVgPainter::image(AssetId, const Rect&) {}

}
