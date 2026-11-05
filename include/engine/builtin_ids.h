#pragma once

// docs/tech/modules/Resources.md

#include <engine/resources/asset_id.h>

#include <array>
#include <cstddef>
#include <span>

namespace engine::builtin {

inline constexpr AssetId shader_unlit{"a0e1b2c3d4f5678901234567890abc01"};
inline constexpr AssetId mesh_quad{"a0e1b2c3d4f5678901234567890abc02"};
inline constexpr AssetId material_unlit{"a0e1b2c3d4f5678901234567890abc03"};
inline constexpr AssetId font_ui{"a0e1b2c3d4f5678901234567890abc04"};
inline constexpr AssetId splash_wind{"a0e1b2c3d4f5678901234567890abc05"};
inline constexpr AssetId font_math{"a0e1b2c3d4f5678901234567890abc06"};
inline constexpr AssetId tree_chevron{"a0e1b2c3d4f5678901234567890abc07"};
// Default theme of dock space chrome (docs/tech/features/Docking.md#host).
inline constexpr AssetId dock_css{"a0e1b2c3d4f5678901234567890abc08"};
// Unit cube with per-face normals (docs/tech/modules/Render.md#primitives).
inline constexpr AssetId mesh_cube{"a0e1b2c3d4f5678901234567890abc09"};
// Lambert shader: directional lights plus ambient from CmdSetView, times vertex color, with cutout and fog
// (docs/tech/modules/Render.md#lit-shader).
inline constexpr AssetId shader_lit{"a0e1b2c3d4f5678901234567890abc0a"};
// White opaque material on shader_lit: depth on, back faces culled, no texture.
inline constexpr AssetId material_lit{"a0e1b2c3d4f5678901234567890abc0b"};
// shader_lit sampling a texture array at MeshVertex::layer (docs/tech/modules/Render.md#texture-arrays).
inline constexpr AssetId shader_lit_array{"a0e1b2c3d4f5678901234567890abc0c"};
// shader_unlit sampling a texture array at MeshVertex::layer, without animated tiles.
inline constexpr AssetId shader_unlit_array{"a0e1b2c3d4f5678901234567890abc0d"};
// shader_lit with a bone palette (docs/tech/modules/Render.md).
inline constexpr AssetId shader_skin_lit{"a0e1b2c3d4f5678901234567890abc0e"};
// 3D unlit with the same bone palette. Not the tile unlit shader.
inline constexpr AssetId shader_skin_unlit{"a0e1b2c3d4f5678901234567890abc0f"};
// Fullscreen grade: rgb *= uParams.rgb, alpha copied (docs/tech/modules/Render.md).
inline constexpr AssetId shader_post_grade{"a0e1b2c3d4f5678901234567890abc10"};
// Reinhard tonemap. The engine appends it when PostStack::hdr is set; a game does not list it.
inline constexpr AssetId shader_post_tonemap{"a0e1b2c3d4f5678901234567890abc11"};
// HDR bloom chain. Fatal on a stack with hdr == false. uParams.x is the threshold, uParams.y the intensity.
inline constexpr AssetId shader_post_bloom{"a0e1b2c3d4f5678901234567890abc12"};
// Tints the scene by the depth texture. Resolving it sets PostEffect::sample_depth.
inline constexpr AssetId shader_post_depth{"a0e1b2c3d4f5678901234567890abc13"};

inline constexpr std::array<AssetId, 19> ids{
        shader_unlit,
        mesh_quad,
        material_unlit,
        font_ui,
        splash_wind,
        font_math,
        tree_chevron,
        dock_css,
        mesh_cube,
        shader_lit,
        material_lit,
        shader_lit_array,
        shader_unlit_array,
        shader_skin_lit,
        shader_skin_unlit,
        shader_post_grade,
        shader_post_tonemap,
        shader_post_bloom,
        shader_post_depth,
};

[[nodiscard]] constexpr std::span<const AssetId> reserved() noexcept {
    return ids;
}

[[nodiscard]] constexpr std::size_t count() noexcept {
    return ids.size();
}

}
