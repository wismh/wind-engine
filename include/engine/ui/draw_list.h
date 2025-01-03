#pragma once

#include <engine/render/commands.h>
#include <engine/resources/asset_id.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <string_view>

namespace engine::ui {

// Game-facing draw surface for `IPaint`. Coordinates are local to the element's content box
// (origin top-left, design px). The engine maps them through the canvas scale/offset; games do not
// call NanoVG or `IUiPainter`.
class IDrawList {
public:
    virtual ~IDrawList() = default;

    virtual void line(glm::vec2 from, glm::vec2 to, glm::vec4 color, float width) = 0;
    virtual void fill_rect(const render::Rect& rect, glm::vec4 color, float radius = 0.0f) = 0;
    virtual void stroke_rect(const render::Rect& rect, glm::vec4 color, float width, float radius = 0.0f) = 0;
    virtual void set_font(AssetId font, float size) = 0;
    virtual void text(std::string_view text, glm::vec2 position, glm::vec4 color) = 0;
    virtual void image(AssetId texture, const render::Rect& rect) = 0;
};

}
