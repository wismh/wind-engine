#include "gltf/cook.h"

#include <engine/resources/model_cook.h>

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace engine::gltf {

void register_gltf_cooker() {
    engine::set_model_cooker([](std::string_view bytes, const std::filesystem::path& directory,
                                    std::span<const engine::ModelMarkerMeta> markers)
                                     -> std::expected<std::vector<std::uint8_t>, std::string> {
        auto cooked = cook_gltf(bytes, directory, markers);
        if (!cooked) {
            return std::unexpected(cooked.error().message);
        }
        return std::move(*cooked);
    });
}

}
