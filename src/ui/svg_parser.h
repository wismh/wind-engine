#pragma once

#include <engine/ui/svg.h>

#include <optional>
#include <string_view>

namespace engine::ui {

// The icon subset: viewBox, g, path, rect, circle, ellipse, line, polyline, polygon. Anything the painter
// cannot draw (text, a gradient, a dash, even-odd fill) rejects the whole file.
[[nodiscard]] std::optional<SvgPicture> parse_svg(std::string_view xml);

}
