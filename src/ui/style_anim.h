#pragma once

#include <engine/ui/document.h>

#include <glm/vec2.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace engine::ui {

// True for `transition*` / `animation*` declarations (not for every animatable property).
[[nodiscard]] bool is_motion_declaration(std::string_view property);

// Parse-time warnings: unknown easing on a timing-function longhand, unknown transition property.
[[nodiscard]] std::vector<std::string> validate_motion_value(std::string_view property, std::string_view value);

void apply_motion_declaration(ComputedStyle& style, std::string_view property, std::string_view value);

// Defined in paint.cpp so keyframe stops reuse the cascade declaration parser.
void apply_style_declaration(ComputedStyle& style, const CssDeclaration& decl);

// Step clocks on `element` against the pre-motion `target`. Fills `motion_shown`,
// `layout_inputs_changed`, and `height_motion_active`. Does not write layout fields.
void advance_motion(
        Element& element, const ComputedStyle& target, const Stylesheet* sheet, float dt, glm::vec2 basis);

void apply_motion_shown(const Element& element, ComputedStyle& style);
void commit_motion_layout(Element& element);
void commit_motion_visuals(Element& element);

}
