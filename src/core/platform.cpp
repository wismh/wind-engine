#include <engine/core/platform.h>

#include <system_error>

namespace engine {

std::filesystem::path packaged_assets_mount() noexcept {
    return std::filesystem::path{"/assets"};
}

std::filesystem::path apk_assets_mount() noexcept {
    return std::filesystem::path{"assets://"};
}

std::filesystem::path default_assets_root(const std::filesystem::path& base_path, Platform platform) {
    if (platform == Platform::Web) {
        return packaged_assets_mount();
    }
    if (platform == Platform::Android) {
        if (base_path.empty()) {
            return apk_assets_mount();
        }
        return base_path / "assets";
    }
    if (base_path.empty()) {
        return {};
    }
    return base_path / "assets";
}

std::filesystem::path default_assets_root(const std::filesystem::path& base_path) {
    return default_assets_root(base_path, current_platform());
}

bool stage_android_assets(const std::filesystem::path& src_root, const std::filesystem::path& dest_root) {
    if (src_root.empty() || dest_root.empty()) {
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(src_root, ec) || ec) {
        return false;
    }
    std::filesystem::create_directories(dest_root, ec);
    if (ec) {
        return false;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(src_root, ec)) {
        if (ec) {
            return false;
        }
        const std::filesystem::path relative = std::filesystem::relative(entry.path(), src_root, ec);
        if (ec || relative.empty()) {
            return false;
        }
        const std::filesystem::path out = dest_root / relative;
        if (entry.is_directory()) {
            std::filesystem::create_directories(out, ec);
        } else if (entry.is_regular_file()) {
            std::filesystem::create_directories(out.parent_path(), ec);
            if (ec) {
                return false;
            }
            std::filesystem::copy_file(entry.path(), out, std::filesystem::copy_options::overwrite_existing, ec);
        }
        if (ec) {
            return false;
        }
    }
    return std::filesystem::is_directory(dest_root);
}

}
