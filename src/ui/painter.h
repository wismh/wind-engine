#pragma once

#include <engine/render/commands.h>
#include <engine/resources/asset_id.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <string_view>

namespace engine::ui {

enum class UiAlign {
    Start,
    Center,
    End,
};

class IUiPainter {
public:
    virtual ~IUiPainter() = default;

    virtual void save() = 0;
    virtual void restore() = 0;
    virtual void scissor(const render::Rect& rect) = 0;
    virtual void set_opacity(float opacity) = 0;
    virtual void fill_rounded_rect(const render::Rect& rect, float radius, glm::vec4 color) = 0;
    virtual void stroke_rounded_rect(const render::Rect& rect, float radius, float width, glm::vec4 color) = 0;
    virtual void set_font(AssetId font, float size) = 0;
    virtual void fill_text(std::string_view text, glm::vec2 position, glm::vec4 color, UiAlign horizontal,
            UiAlign vertical) = 0;
    virtual void image(AssetId texture, const render::Rect& rect) = 0;
};

struct UiPaintInput {
    render::Rect canvas_rect{};
    glm::vec2 pointer{};
    bool pointer_down = false;
};

void apply_layout_style(Element& root, const Stylesheet* sheet);
void paint_document(UiDocument& document, const Stylesheet* stylesheet, IUiPainter& painter, const UiPaintInput& input);

}
