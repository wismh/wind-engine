#pragma once

#include <engine/ui/document.h>

#include <glm/vec2.hpp>

#include <optional>
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

// The value of `prop` at progress `t` (0..1, easing already applied) in `keyframes`, from the compiled tracks: nothing
// is parsed per call. Empty when no stop sets `prop`.
[[nodiscard]] std::optional<MotionValue> sample_keyframes(const Keyframes &keyframes, MotionProp prop, float t,
                                                          glm::vec2 basis);

// The same value, with the declarations of every stop parsed on each call (what sampling did before tracks). Kept as
// the oracle the tests compare `sample_keyframes` with; the engine does not call it.
[[nodiscard]] std::optional<MotionValue> sample_keyframes_from_declarations(const Keyframes &keyframes, MotionProp prop,
                                                                            float t, glm::vec2 basis);

// Step clocks on `element` against the pre-motion `target`. Fills `motion_shown` with running
// transitions and @keyframes samples (a transition already at its goal is omitted), plus
// `layout_inputs_changed` and `height_motion_active`. Does not write layout fields.
void advance_motion(
        Element& element, const ComputedStyle& target, const Stylesheet* sheet, float dt, glm::vec2 basis);

void apply_motion_shown(const Element& element, ComputedStyle& style);
void commit_motion_layout(Element& element);
void commit_motion_visuals(Element& element);

}
