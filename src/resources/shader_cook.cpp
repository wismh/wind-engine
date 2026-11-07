#include <engine/resources/shader_cook.h>

namespace engine {
namespace {

ShaderCookFn& cooker_slot() {
    static ShaderCookFn cooker;
    return cooker;
}

}

void set_shader_cooker(ShaderCookFn cooker) {
    cooker_slot() = std::move(cooker);
}

std::expected<std::vector<std::uint8_t>, std::string> cook_shader_asset(
        std::string_view xml, const std::filesystem::path& directory) {
    const ShaderCookFn& cooker = cooker_slot();
    if (!cooker) {
        return std::unexpected("shader cooker is not registered");
    }
    return cooker(xml, directory);
}

}
