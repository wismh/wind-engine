#include <engine/core/platform.h>

#include <string>
#include <system_error>

#if defined(__ANDROID__)
#include <engine/resources/meta.h>

#include <SDL3/SDL.h>

#include <fstream>
#include <iterator>
#include <vector>
#endif

namespace engine {

std::filesystem::path packaged_assets_mount() noexcept {
    return std::filesystem::path{"/assets"};
}

std::string apk_assets_mount() noexcept {
    return "assets://";
}

std::filesystem::path default_assets_root(const std::filesystem::path& base_path, Platform platform) {
    if (platform == Platform::Web) {
        return packaged_assets_mount();
    }
    if (platform == Platform::Android) {
        if (base_path.empty()) {
            // assets:// is not a portable std::filesystem::path (drive-letter parse).
            return {};
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

#if defined(__ANDROID__)
namespace {

bool copy_sdl_io_file(const char* sdl_path, const std::filesystem::path& dest) {
    SDL_IOStream* io = SDL_IOFromFile(sdl_path, "rb");
    if (io == nullptr) {
        return false;
    }
    const Sint64 size = SDL_GetIOSize(io);
    if (size < 0) {
        SDL_CloseIO(io);
        return false;
    }
    std::vector<char> buf(static_cast<std::size_t>(size));
    if (size > 0 && SDL_ReadIO(io, buf.data(), static_cast<std::size_t>(size)) != static_cast<std::size_t>(size)) {
        SDL_CloseIO(io);
        return false;
    }
    SDL_CloseIO(io);
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    if (ec) {
        return false;
    }
    std::ofstream out(dest, std::ios::binary);
    if (!out) {
        return false;
    }
    if (!buf.empty()) {
        out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    }
    return static_cast<bool>(out);
}

// This SDL3 build has no Android directory enumeration, so stage each catalog entry by its known path.
void stage_catalog_assets(
        const std::filesystem::path& catalog_file, const std::string& sdl_prefix, const std::filesystem::path& dest_root) {
    std::ifstream in(catalog_file, std::ios::binary);
    if (!in) {
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto parsed = parse_cooked_catalog(text);
    if (!parsed) {
        return;
    }
    for (const CatalogEntry& entry : parsed->entries()) {
        copy_sdl_io_file((sdl_prefix + entry.relative_path).c_str(), dest_root / entry.relative_path);
    }
}

std::filesystem::path android_runtime_assets_root(const std::filesystem::path& base) {
    const char* storage = SDL_GetAndroidInternalStoragePath();
    const std::filesystem::path internal = storage != nullptr ? std::filesystem::path{storage} : std::filesystem::path{};
    const std::filesystem::path dest = default_assets_root(internal, Platform::Android);

    std::error_code ec;
    if (std::filesystem::exists(dest / "engine" / "catalog.toml", ec)) {
        return dest;
    }

    const std::string generic = base.generic_string();
    if (!base.empty() && generic.find("assets:") == std::string::npos && generic != "." && generic != "./") {
        const std::filesystem::path src = default_assets_root(base, Platform::Native);
        if (std::filesystem::is_directory(src, ec)) {
            stage_android_assets(src, dest);
            return dest;
        }
    }

    copy_sdl_io_file("catalog.toml", dest / "catalog.toml");
    copy_sdl_io_file("engine/catalog.toml", dest / "engine" / "catalog.toml");
    stage_catalog_assets(dest / "catalog.toml", "", dest);
    stage_catalog_assets(dest / "engine" / "catalog.toml", "engine/", dest / "engine");
    return dest;
}

}

#endif

std::filesystem::path runtime_assets_root(const std::filesystem::path& base_path) {
#if defined(__ANDROID__)
    return android_runtime_assets_root(base_path);
#else
    return default_assets_root(base_path);
#endif
}

}
