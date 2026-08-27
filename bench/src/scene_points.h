#pragma once

#include <engine/render/commands.h>
#include <engine/ui/document.h>

#include <glm/vec2.hpp>

#include <optional>
#include <string_view>
#include <vector>

namespace bench {

// The class that marks an element the hover mode may move the pointer onto.
inline constexpr std::string_view kHoverClass = "hot";

// The middle of the part of element `id` that lies inside `bounds`, in canvas layout space (after scroll and Viewport
// cameras). nullopt when there is no such element or none of it is inside `bounds`.
[[nodiscard]] std::optional<glm::vec2> element_point(engine::ui::Element& root, std::string_view id,
        const engine::render::Rect& bounds);

// The middle of every `hot` element (generated rows included) whose visible part inside `bounds` is hit by a pointer
// at that middle, in document order. An element covered by another interactive one is left out.
[[nodiscard]] std::vector<glm::vec2> hover_points(engine::ui::Element& root, const engine::render::Rect& bounds);

}
