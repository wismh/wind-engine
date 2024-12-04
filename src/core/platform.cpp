#include <engine/core/platform.h>

namespace engine {

std::filesystem::path packaged_assets_mount() noexcept {
    return std::filesystem::path{"/assets"};
}

std::filesystem::path default_assets_root(const std::filesystem::path& base_path, Platform platform) {
    if (platform == Platform::Web) {
        return packaged_assets_mount();
    }
    if (base_path.empty()) {
        return {};
    }
    return base_path / "assets";
}

std::filesystem::path default_assets_root(const std::filesystem::path& base_path) {
    return default_assets_root(base_path, current_platform());
}

}
