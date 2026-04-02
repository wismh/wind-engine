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
// Lambert shader: directional lights plus ambient from CmdSetView (docs/tech/modules/Render.md#lit-shader).
inline constexpr AssetId shader_lit{"a0e1b2c3d4f5678901234567890abc0a"};
// White opaque material on shader_lit: depth on, back faces culled, no texture.
inline constexpr AssetId material_lit{"a0e1b2c3d4f5678901234567890abc0b"};

inline constexpr std::array<AssetId, 11> ids{
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
};

[[nodiscard]] constexpr std::span<const AssetId> reserved() noexcept {
    return ids;
}

[[nodiscard]] constexpr std::size_t count() noexcept {
    return ids.size();
}

}
