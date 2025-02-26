#pragma once

#include <engine/render/graphics.h>
#include <engine/render/sprite.h>
#include <engine/resources/meta.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine {

class SpriteSheet {
public:
    std::shared_ptr<render::ITexture> texture;
    float pixels_per_unit = 100.0f;
    std::unordered_map<std::string, SpriteMeta> sprites;

    [[nodiscard]] std::optional<render::Sprite> get(std::string_view name) const {
        const auto it = sprites.find(std::string(name));
        if (it == sprites.end()) {
            return std::nullopt;
        }
        const SpriteMeta& meta = it->second;
        render::Sprite sprite;
        sprite.texture = texture;
        const float tex_w = texture ? static_cast<float>(texture->width()) : 0.0f;
        const float tex_h = texture ? static_cast<float>(texture->height()) : 0.0f;
        if (tex_w > 0.0f && tex_h > 0.0f) {
            sprite.tiling = glm::vec2{
                    static_cast<float>(meta.rect.w) / tex_w,
                    static_cast<float>(meta.rect.h) / tex_h,
            };
            sprite.offset = glm::vec2{
                    static_cast<float>(meta.rect.x) / tex_w,
                    (tex_h - static_cast<float>(meta.rect.y + meta.rect.h)) / tex_h,
            };
        }
        sprite.pixel_size = glm::vec2{
                static_cast<float>(meta.rect.w),
                static_cast<float>(meta.rect.h),
        };
        sprite.pixels_per_unit = meta.pixels_per_unit.value_or(pixels_per_unit);
        sprite.pivot = meta.pivot;
        return sprite;
    }

    [[nodiscard]] bool contains(std::string_view name) const {
        return sprites.contains(std::string(name));
    }
};

}
