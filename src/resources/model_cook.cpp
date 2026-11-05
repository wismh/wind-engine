#include <engine/resources/model_cook.h>

namespace engine {
namespace {

ModelCookFn& cooker_slot() {
    static ModelCookFn cooker;
    return cooker;
}

}

void set_model_cooker(ModelCookFn cooker) {
    cooker_slot() = std::move(cooker);
}

std::expected<std::vector<std::uint8_t>, std::string> cook_model_asset(
        std::string_view bytes, const std::filesystem::path& directory, std::span<const ModelMarkerMeta> markers) {
    const ModelCookFn& cooker = cooker_slot();
    if (!cooker) {
        return std::unexpected("model cooker is not registered");
    }
    return cooker(bytes, directory, markers);
}

}
