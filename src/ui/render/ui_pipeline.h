#pragma once

// The fixed pipeline a UI device draws every batch with, in backend-neutral terms. A Vulkan, Metal, or D3D device
// builds its pipeline object from these tables and its shader from fragment.h; it never reads the OpenGL device.

#include "ui/render/ui_vertex.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace engine::ui {

enum class UiVertexFormat : std::uint8_t {
    Float2,
    Float4,
    // Four unsigned bytes read as normalized 0..1 floats (RGBA, red in the lowest address).
    UNorm8x4,
    // One 32-bit unsigned integer, read as an integer (glVertexAttribIPointer, not normalized, not converted).
    UInt,
};

struct UiVertexAttribute {
    // The shader input this attribute feeds; also the GLSL `in` name.
    const char* name;
    std::uint32_t location;
    UiVertexFormat format;
    std::uint32_t offset;
};

inline constexpr std::uint32_t kUiVertexStride = sizeof(UiVertex);

inline constexpr std::array<UiVertexAttribute, 8> kUiVertexAttributes{{
        {"a_position", 0, UiVertexFormat::Float2, offsetof(UiVertex, position)},
        {"a_uv", 1, UiVertexFormat::Float2, offsetof(UiVertex, uv)},
        {"a_local", 2, UiVertexFormat::Float2, offsetof(UiVertex, local)},
        {"a_shape", 3, UiVertexFormat::Float4, offsetof(UiVertex, shape)},
        {"a_clip", 4, UiVertexFormat::Float4, offsetof(UiVertex, clip)},
        {"a_aux", 5, UiVertexFormat::Float2, offsetof(UiVertex, aux)},
        {"a_color", 6, UiVertexFormat::UNorm8x4, offsetof(UiVertex, color)},
        {"a_kind", 7, UiVertexFormat::UInt, offsetof(UiVertex, kind)},
}};

// Indices are 32-bit, triangle lists, every quad two triangles (0 1 2, 0 2 3) of its 4 vertices.
using UiIndex = std::uint32_t;

// Blend and raster state, the same for every batch: premultiplied alpha over, color and alpha alike
// (src * 1 + dst * (1 - src.a)), no depth test or write, no stencil, no face culling, no scissor rectangle (clips are
// per-vertex), viewport the whole target. Varyings: every attribute but `a_kind` interpolates linearly
// (perspective-correct and linear agree, the projection being affine); `a_kind` is flat.
enum class UiBlendFactor : std::uint8_t {
    One,
    OneMinusSrcAlpha,
};

struct UiBlendState {
    UiBlendFactor src_color = UiBlendFactor::One;
    UiBlendFactor dst_color = UiBlendFactor::OneMinusSrcAlpha;
    UiBlendFactor src_alpha = UiBlendFactor::One;
    UiBlendFactor dst_alpha = UiBlendFactor::OneMinusSrcAlpha;
};

inline constexpr UiBlendState kUiBlend{};

// The four textures a batch binds at once, one sampler each, so text, path masks, gradients, and images mix within a
// batch; a batch ends only when a slot already bound in it needs another page. All four sample bilinear, no mipmaps,
// clamp to edge (wrapping is done in the shader where needed).
enum class UiTextureSlot : std::uint8_t {
    // R8 coverage, GlyphAtlas pages.
    Glyph = 0,
    // R8 coverage, MaskAtlas pages (fill_path and stroke_path).
    Mask = 1,
    // RGBA8 premultiplied, RampAtlas pages (gradients).
    Ramp = 2,
    // RGBA8 straight alpha, ImageAtlas pages (the shader premultiplies after the fetch).
    Image = 3,
};

inline constexpr std::size_t kUiTextureSlotCount = 4;

// The sampler names, in UiTextureSlot order.
inline constexpr std::array<const char*, kUiTextureSlotCount> kUiSamplerNames{
        "u_glyphs",
        "u_masks",
        "u_ramps",
        "u_images",
};

}
