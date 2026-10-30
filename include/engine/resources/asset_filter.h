#pragma once

#include <engine/resources/meta.h>

#include <optional>
#include <string>
#include <type_traits>

namespace engine {

struct AssetFilter {
    std::optional<ImporterKind> importer{};
    std::string object_type{};

    bool operator==(const AssetFilter&) const noexcept = default;
};

// Forward declarations for engine types so asset_filter_for does not pull in heavy headers.
namespace render {
class ITexture;
struct TextureDesc;
class IMesh;
class IMaterial;
class IShader;
struct SpriteAnimationClip;
struct TilemapAsset;
} // namespace render

struct Font;
struct Sound;
class SpriteSheet;
struct TextAsset;

namespace ui {
struct UiDocument;
struct Stylesheet;
struct SvgPicture;
} // namespace ui

template<typename T>
struct CustomAssetFilter {
    static constexpr bool kHas = false;
    static AssetFilter filter() { return AssetFilter{.importer = ImporterKind::Object}; }
};

template<typename T>
[[nodiscard]] inline AssetFilter asset_filter_for() {
    if constexpr (CustomAssetFilter<T>::kHas) {
        return CustomAssetFilter<T>::filter();
    } else if constexpr (std::is_same_v<T, render::ITexture> || std::is_same_v<T, render::TextureDesc> ||
                         std::is_same_v<T, SpriteSheet>) {
        return AssetFilter{.importer = ImporterKind::Texture};
    } else if constexpr (std::is_same_v<T, render::IMesh>) {
        return AssetFilter{.importer = ImporterKind::Mesh};
    } else if constexpr (std::is_same_v<T, render::IMaterial>) {
        return AssetFilter{.importer = ImporterKind::Material};
    } else if constexpr (std::is_same_v<T, render::IShader>) {
        return AssetFilter{.importer = ImporterKind::Shader};
    } else if constexpr (std::is_same_v<T, Font>) {
        return AssetFilter{.importer = ImporterKind::Font};
    } else if constexpr (std::is_same_v<T, Sound>) {
        return AssetFilter{.importer = ImporterKind::Audio};
    } else if constexpr (std::is_same_v<T, render::SpriteAnimationClip>) {
        return AssetFilter{.importer = ImporterKind::Animation};
    } else if constexpr (std::is_same_v<T, render::TilemapAsset>) {
        return AssetFilter{.importer = ImporterKind::Tilemap};
    } else if constexpr (std::is_same_v<T, ui::UiDocument>) {
        return AssetFilter{.importer = ImporterKind::Ui};
    } else if constexpr (std::is_same_v<T, ui::Stylesheet>) {
        return AssetFilter{.importer = ImporterKind::Css};
    } else if constexpr (std::is_same_v<T, ui::SvgPicture>) {
        return AssetFilter{.importer = ImporterKind::Svg};
    } else if constexpr (std::is_same_v<T, TextAsset>) {
        return AssetFilter{.importer = ImporterKind::Data};
    } else {
        return AssetFilter{.importer = ImporterKind::Object};
    }
}

} // namespace engine
