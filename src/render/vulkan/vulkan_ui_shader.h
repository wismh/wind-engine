#pragma once

// docs/tech/modules/Render.md
//
// The UI uber-shader in Slang, compiled to SPIR-V for VulkanUiDevice. Implements src/ui/render/fragment.h
// line for line with the vertex layout and four sampler slots of src/ui/render/ui_pipeline.h.

#include <string_view>

namespace engine::render {

inline constexpr std::string_view kVulkanUiSlangShader = R"(
struct VertexInput {
    [[vk::location(0)]] float2 a_position : POSITION;
    [[vk::location(1)]] float2 a_uv       : TEXCOORD0;
    [[vk::location(2)]] float2 a_local    : TEXCOORD1;
    [[vk::location(3)]] float4 a_shape    : TEXCOORD2;
    [[vk::location(4)]] float4 a_clip     : TEXCOORD3;
    [[vk::location(5)]] float2 a_aux      : TEXCOORD4;
    [[vk::location(6)]] float4 a_color    : COLOR0;
    [[vk::location(7)]] uint   a_kind     : BLENDINDICES0;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 v_uv    : TEXCOORD0;
    [[vk::location(1)]] float2 v_local : TEXCOORD1;
    [[vk::location(2)]] float4 v_shape : TEXCOORD2;
    [[vk::location(3)]] float4 v_clip  : TEXCOORD3;
    [[vk::location(4)]] float2 v_aux   : TEXCOORD4;
    [[vk::location(5)]] float4 v_color : COLOR0;
    [[vk::location(6)]] nointerpolation uint v_kind : BLENDINDICES0;
};

// begin_frame's width x height, logical units (binding 0, set 0 UBO).
uniform float2 u_view_size;

// UiTextureSlot order: bindings 1..4 in set 0.
[[vk::binding(1, 0)]] Sampler2D u_glyphs;
[[vk::binding(2, 0)]] Sampler2D u_masks;
[[vk::binding(3, 0)]] Sampler2D u_ramps;
[[vk::binding(4, 0)]] Sampler2D u_images;

static const float kRampWidth = 256.0;
static const float kPi = 3.14159265358979323846;

// UiShapeKind.
static const uint kFill = 0u;
static const uint kStroke = 1u;
static const uint kLine = 2u;
static const uint kArc = 3u;
static const uint kGlyph = 4u;
static const uint kMask = 5u;
static const uint kImage = 6u;
static const uint kImageRepeat = 7u;
static const uint kLinearGradient = 8u;
static const uint kRadialGradient = 9u;
static const uint kConicGradient = 10u;

float box_distance(float2 p, float2 half_size, float2 radii) {
    float2 ap = abs(p);
    float sharp = max(ap.x - half_size.x, ap.y - half_size.y);
    if (radii.x <= 0.0 || radii.y <= 0.0) {
        return sharp;
    }
    float2 q = ap - (half_size - radii);
    if (q.x > 0.0 && q.y > 0.0) {
        float k0 = length(q / radii);
        float k1 = length(q / (radii * radii));
        return k0 * (k0 - 1.0) / k1;
    }
    return sharp;
}

float clip_coverage(float4 c) {
    return saturate(0.5 - (abs(c.x) - c.z)) * saturate(0.5 - (abs(c.y) - c.w));
}

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput out_v;
    out_v.v_uv = input.a_uv;
    out_v.v_local = input.a_local;
    out_v.v_shape = input.a_shape;
    out_v.v_clip = input.a_clip;
    out_v.v_aux = input.a_aux;
    out_v.v_color = input.a_color;
    out_v.v_kind = input.a_kind;
    // Origin top left, y down (drawn with negative viewport height so clip +Y is up).
    out_v.position = float4(
        2.0 * input.a_position.x / u_view_size.x - 1.0,
        1.0 - 2.0 * input.a_position.y / u_view_size.y,
        0.0,
        1.0);
    return out_v;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    float2 half_size = input.v_shape.xy;
    float2 radii = input.v_shape.zw;
    float4 color = input.v_color;
    uint v_kind = input.v_kind;

    if (v_kind == kFill) {
        color *= saturate(0.5 - box_distance(input.v_local, half_size, radii));
    } else if (v_kind == kStroke) {
        color *= saturate(input.v_aux.x + 0.5 - abs(box_distance(input.v_local, half_size, radii)));
    } else if (v_kind == kLine) {
        color *= saturate(input.v_shape.y + 0.5 - abs(input.v_local.y)) *
                 saturate(input.v_shape.x + 0.5 - abs(input.v_local.x));
    } else if (v_kind == kArc) {
        float r = length(input.v_local);
        float cov = saturate(input.v_shape.y + 0.5 - abs(r - input.v_shape.x));
        if (input.v_shape.z < kPi) {
            float gap = input.v_shape.z - atan2(abs(input.v_local.x), input.v_local.y);
            if (gap < kPi * 0.5) {
                cov *= saturate(0.5 + r * sin(gap));
            }
        }
        color *= cov;
    } else if (v_kind == kGlyph) {
        color *= u_glyphs.Sample(input.v_uv).r;
    } else if (v_kind == kMask) {
        color *= u_masks.Sample(input.v_uv).r;
    } else if (v_kind == kImage || v_kind == kImageRepeat) {
        float2 t = v_kind == kImage ? clamp(input.v_uv, float2(0.0, 0.0), float2(1.0, 1.0)) : frac(input.v_uv);
        float4 texel = u_images.Sample(input.v_shape.zw + t * input.v_aux);
        float4 premultiplied = float4(texel.rgb * texel.a, texel.a);
        color *= premultiplied * saturate(0.5 - box_distance(input.v_local, half_size, float2(0.0, 0.0)));
    } else {
        float t = 0.0;
        if (v_kind == kLinearGradient) {
            t = input.v_uv.x;
        } else if (v_kind == kRadialGradient) {
            t = length(input.v_uv) + input.v_aux.y;
        } else {
            float turns = atan2(input.v_uv.x, -input.v_uv.y) / (2.0 * kPi);
            t = turns - floor(turns);
        }
        float2 ramp_uv = float2((saturate(t) * (kRampWidth - 1.0) + 0.5) / kRampWidth, input.v_aux.x);
        float4 texel = u_ramps.Sample(ramp_uv);
        color *= texel * saturate(0.5 - box_distance(input.v_local, half_size, radii));
    }
    return color * clip_coverage(input.v_clip);
}
)";

} // namespace engine::render
