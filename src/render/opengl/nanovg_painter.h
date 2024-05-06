#pragma once

#include "ui/painter.h"

#include <engine/render/graphic_factory.h>
#include <engine/resources/font.h>

#include <memory>
#include <string>
#include <vector>

namespace engine::render {

class NanoVgPainter final : public ui::IUiPainter {
public:
    NanoVgPainter();
    ~NanoVgPainter() override;

    NanoVgPainter(const NanoVgPainter&) = delete;
    NanoVgPainter& operator=(const NanoVgPainter&) = delete;

    [[nodiscard]] bool create();
    [[nodiscard]] bool load_ui_font(const Font& font);
    [[nodiscard]] bool add_font(AssetId id, const Font& font);
    [[nodiscard]] bool add_image(AssetId id, const TextureDesc& desc);
    void destroy();
    void begin_frame(float width, float height, float pixel_ratio = 1.0f);
    void end_frame();

    void save() override;
    void restore() override;
    void scissor(const Rect& rect) override;
    void apply_transform(glm::vec2 center, float rotation_radians, float scale) override;
    void set_opacity(float opacity) override;
    void fill_rounded_rect(const Rect& rect, float radius, glm::vec4 color) override;
    void stroke_rounded_rect(const Rect& rect, float radius, float width, glm::vec4 color) override;
    void set_font(AssetId font, float size) override;
    void fill_text(std::string_view text, glm::vec2 position, glm::vec4 color, ui::UiAlign horizontal,
            ui::UiAlign vertical) override;
    void image(AssetId texture, const Rect& rect) override;
    [[nodiscard]] glm::vec2 measure_text(std::string_view text, AssetId font, float size) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
