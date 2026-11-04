#pragma once

// docs/tech/modules/Render.md#texture-arrays

#include <engine/render/graphics.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::render {

// A texture array asset (`importer = "texture_array"`): one layer per `[[sprites]]` frame of the PNG, in `.meta` order.
// `AssetsDb::get<TextureArray>(id)`; `texture` is the same object as `get<ITexture>(id)`.
struct TextureArray {
    std::shared_ptr<ITexture> texture;
    // The frame name of each layer: `layer_names[i]` is layer i.
    std::vector<std::string> layer_names;

    // The layer of the frame called `name`, for `MeshVertex::layer`; nullopt when there is no such frame.
    [[nodiscard]] std::optional<int> layer(std::string_view name) const {
        for (std::size_t i = 0; i < layer_names.size(); ++i) {
            if (layer_names[i] == name) {
                return static_cast<int>(i);
            }
        }
        return std::nullopt;
    }
};

}
