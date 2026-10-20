#pragma once

// The UI uber-shader, GLSL 330 core (adapt_glsl rewrites the version line for 300 es; both stages already declare
// highp, which 330 core accepts and ignores). The fragment stage is src/ui/render/fragment.h line for line: same
// names, same order, same constants. A SPIR-V, MSL, or HLSL port translates these two strings and nothing else; the
// attribute locations are ui_pipeline.h's kUiVertexAttributes and the sampler names its kUiSamplerNames.

#include <string_view>

namespace engine::render {

inline constexpr std::string_view kUiVertexShader = R"(#version 330 core
precision highp float;
precision highp int;

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec2 a_local;
layout(location = 3) in vec4 a_shape;
layout(location = 4) in vec4 a_clip;
layout(location = 5) in vec2 a_aux;
layout(location = 6) in vec4 a_color;
layout(location = 7) in uint a_kind;

// begin_frame's width x height, logical units.
uniform vec2 u_view_size;

out vec2 v_uv;
out vec2 v_local;
out vec4 v_shape;
out vec4 v_clip;
out vec2 v_aux;
out vec4 v_color;
flat out uint v_kind;

void main() {
    v_uv = a_uv;
    v_local = a_local;
    v_shape = a_shape;
    v_clip = a_clip;
    v_aux = a_aux;
    v_color = a_color;
    v_kind = a_kind;
    // Origin top left, y down.
    gl_Position = vec4(2.0 * a_position.x / u_view_size.x - 1.0, 1.0 - 2.0 * a_position.y / u_view_size.y, 0.0, 1.0);
}
)";

inline constexpr std::string_view kUiFragmentShader = R"(#version 330 core
precision highp float;
precision highp int;

// fragment.h: kRampWidth.
const float kRampWidth = 256.0;
const float kPi = 3.14159265358979323846;

// UiShapeKind.
const uint kFill = 0u;
const uint kStroke = 1u;
const uint kLine = 2u;
const uint kArc = 3u;
const uint kGlyph = 4u;
const uint kMask = 5u;
const uint kImage = 6u;
const uint kImageRepeat = 7u;
const uint kLinearGradient = 8u;
const uint kRadialGradient = 9u;
const uint kConicGradient = 10u;

// UiTextureSlot order: units 0..3.
uniform sampler2D u_glyphs;
uniform sampler2D u_masks;
uniform sampler2D u_ramps;
uniform sampler2D u_images;

in vec2 v_uv;
in vec2 v_local;
in vec4 v_shape;
in vec4 v_clip;
in vec2 v_aux;
in vec4 v_color;
flat in uint v_kind;

layout(location = 0) out vec4 o_color;

float saturate(float x) {
    return clamp(x, 0.0, 1.0);
}

// box_distance
float box_distance(vec2 p, vec2 half_size, vec2 radii) {
    vec2 ap = abs(p);
    float sharp = max(ap.x - half_size.x, ap.y - half_size.y);
    if (radii.x <= 0.0 || radii.y <= 0.0) {
        return sharp;
    }
    vec2 q = ap - (half_size - radii);
    if (q.x > 0.0 && q.y > 0.0) {
        float k0 = length(q / radii);
        float k1 = length(q / (radii * radii));
        return k0 * (k0 - 1.0) / k1;
    }
    return sharp;
}

// clip_coverage
float clip_coverage() {
    vec4 c = v_clip;
    return saturate(0.5 - (abs(c.x) - c.z)) * saturate(0.5 - (abs(c.y) - c.w));
}

// fragment_fetch, then shade_fragment
void main() {
    vec2 half_size = v_shape.xy;
    vec2 radii = v_shape.zw;
    vec4 color = v_color;
    if (v_kind == kFill) {
        color *= saturate(0.5 - box_distance(v_local, half_size, radii));
    } else if (v_kind == kStroke) {
        color *= saturate(v_aux.x + 0.5 - abs(box_distance(v_local, half_size, radii)));
    } else if (v_kind == kLine) {
        color *= saturate(v_shape.y + 0.5 - abs(v_local.y)) * saturate(v_shape.x + 0.5 - abs(v_local.x));
    } else if (v_kind == kArc) {
        float r = length(v_local);
        float cov = saturate(v_shape.y + 0.5 - abs(r - v_shape.x));
        if (v_shape.z < kPi) {
            float gap = v_shape.z - atan(abs(v_local.x), v_local.y);
            if (gap < kPi * 0.5) {
                cov *= saturate(0.5 + r * sin(gap));
            }
        }
        color *= cov;
    } else if (v_kind == kGlyph) {
        color *= texture(u_glyphs, v_uv).r;
    } else if (v_kind == kMask) {
        color *= texture(u_masks, v_uv).r;
    } else if (v_kind == kImage || v_kind == kImageRepeat) {
        vec2 t = v_kind == kImage ? clamp(v_uv, 0.0, 1.0) : fract(v_uv);
        vec4 texel = texture(u_images, v_shape.zw + t * v_aux);
        vec4 premultiplied = vec4(texel.rgb * texel.a, texel.a);
        color *= premultiplied * saturate(0.5 - box_distance(v_local, half_size, vec2(0.0)));
    } else {
        float t = 0.0;
        if (v_kind == kLinearGradient) {
            t = v_uv.x;
        } else if (v_kind == kRadialGradient) {
            t = length(v_uv) + v_aux.y;
        } else {
            float turns = atan(v_uv.x, -v_uv.y) / (2.0 * kPi);
            t = turns - floor(turns);
        }
        vec4 texel = texture(u_ramps, vec2((saturate(t) * (kRampWidth - 1.0) + 0.5) / kRampWidth, v_aux.x));
        color *= texel * saturate(0.5 - box_distance(v_local, half_size, radii));
    }
    o_color = color * clip_coverage();
}
)";

}
