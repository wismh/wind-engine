#pragma once

#include <engine/resources/asset_id.h>

#include <array>
#include <cstddef>
#include <span>

namespace engine::builtin {

inline constexpr AssetId shader_unlit{"a0e1b2c3d4f5678901234567890abc01"};
inline constexpr AssetId mesh_quad{"a0e1b2c3d4f5678901234567890abc02"};
inline constexpr AssetId material_unlit{"a0e1b2c3d4f5678901234567890abc03"};
inline constexpr AssetId font_ui{"a0e1b2c3d4f5678901234567890abc04"};

inline constexpr std::array<AssetId, 4> ids{
        shader_unlit,
        mesh_quad,
        material_unlit,
        font_ui,
};

[[nodiscard]] constexpr std::span<const AssetId> reserved() noexcept {
    return ids;
}

[[nodiscard]] constexpr std::size_t count() noexcept {
    return ids.size();
}

}
