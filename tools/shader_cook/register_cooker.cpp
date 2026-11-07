#include "shader_cook/cook.h"

#include "shader_cook/parse_shader.h"

#include <engine/resources/shader_blob.h>
#include <engine/resources/shader_cook.h>

#include <utility>

namespace engine::shader_cook {

void register_shader_cooker() {
    engine::set_shader_cooker([](std::string_view xml, const std::filesystem::path& directory)
                                      -> std::expected<std::vector<std::uint8_t>, std::string> {
        auto desc = engine::parse_shader(xml, directory);
        if (!desc) {
            return std::unexpected(std::move(desc.error()));
        }
        return engine::encode_shader_blob(*desc);
    });
}

}
