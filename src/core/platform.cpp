#include <engine/core/platform.h>

#include <string>
#include <system_error>

#if (defined(ENGINE_WITH_WINDOW) && ENGINE_WITH_WINDOW) || defined(__ANDROID__)
#include <SDL3/SDL.h>
#endif

#if defined(ENGINE_WITH_WINDOW) && ENGINE_WITH_WINDOW
#include <memory>
#endif

#if defined(__ANDROID__)
#include <engine/resources/meta.h>

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

namespace {

bool eq_ci_ascii(std::string_view value, std::string_view literal) {
    if (value.size() != literal.size()) {
        return false;
    }
    for (std::size_t i = 0; i < value.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(value[i]);
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<unsigned char>(c - 'A' + 'a');
        }
        if (c != static_cast<unsigned char>(literal[i])) {
            return false;
        }
    }
    return true;
}

bool is_com_or_lpt(std::string_view base) {
    if (base.size() != 4) {
        return false;
    }
    const auto lower = [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c - 'A' + 'a') : c;
    };
    const unsigned char a = lower(static_cast<unsigned char>(base[0]));
    const unsigned char b = lower(static_cast<unsigned char>(base[1]));
    const unsigned char c = lower(static_cast<unsigned char>(base[2]));
    const bool com = a == 'c' && b == 'o' && c == 'm';
    const bool lpt = a == 'l' && b == 'p' && c == 't';
    const unsigned char digit = static_cast<unsigned char>(base[3]);
    return (com || lpt) && digit >= '1' && digit <= '9';
}

// "CON.txt" is the same device as "CON" on Windows. The stem is the part before the first dot.
bool is_windows_reserved(std::string_view name) {
    const auto dot = name.find('.');
    const std::string_view base = dot == std::string_view::npos ? name : name.substr(0, dot);
    return eq_ci_ascii(base, "con") || eq_ci_ascii(base, "prn") || eq_ci_ascii(base, "aux") ||
            eq_ci_ascii(base, "nul") || is_com_or_lpt(base);
}

// Bytes consumed by one non-ASCII code point starting at i, or 0 when the sequence is ill-formed.
int utf8_non_ascii(std::string_view text, std::size_t i) {
    const auto lead = static_cast<unsigned char>(text[i]);
    int need = 0;
    unsigned int min_cp = 0;
    unsigned int cp = 0;
    if ((lead & 0xE0) == 0xC0) {
        need = 2;
        min_cp = 0x80;
        cp = lead & 0x1Fu;
    } else if ((lead & 0xF0) == 0xE0) {
        need = 3;
        min_cp = 0x800;
        cp = lead & 0x0Fu;
    } else if ((lead & 0xF8) == 0xF0) {
        need = 4;
        min_cp = 0x10000;
        cp = lead & 0x07u;
    } else {
        return 0;
    }
    if (i + static_cast<std::size_t>(need) > text.size()) {
        return 0;
    }
    for (int n = 1; n < need; ++n) {
        const auto cont = static_cast<unsigned char>(text[i + static_cast<std::size_t>(n)]);
        if ((cont & 0xC0) != 0x80) {
            return 0;
        }
        cp = (cp << 6) | (cont & 0x3Fu);
    }
    if (cp < min_cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return 0;
    }
    return need;
}

std::error_code segment_error(std::string_view name) {
    if (name.empty() || name.front() == ' ' || name.back() == ' ' || name.back() == '.') {
        return std::make_error_code(std::errc::invalid_argument);
    }
    for (std::size_t i = 0; i < name.size();) {
        const auto c = static_cast<unsigned char>(name[i]);
        if (c < 0x80) {
            if (c < 0x20 || c == 0x7F || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
                    c == '<' || c == '>' || c == '|') {
                return std::make_error_code(std::errc::invalid_argument);
            }
            ++i;
            continue;
        }
        const int width = utf8_non_ascii(name, i);
        if (width == 0) {
            return std::make_error_code(std::errc::invalid_argument);
        }
        i += static_cast<std::size_t>(width);
    }
    if (is_windows_reserved(name)) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    return {};
}

#if defined(ENGINE_WITH_WINDOW) && ENGINE_WITH_WINDOW

struct SdlFree {
    void operator()(char* p) const noexcept {
        SDL_free(p);
    }
};

std::expected<std::filesystem::path, std::error_code> user_data_directory_from_sdl(
        std::string_view organization, std::string_view application) {
    const std::string org{organization};
    const std::string app{application};
    std::unique_ptr<char, SdlFree> raw(SDL_GetPrefPath(org.c_str(), app.c_str()));
    if (raw == nullptr) {
        return std::unexpected(std::make_error_code(std::errc::io_error));
    }
    // SDL hands back UTF-8. path(const char*) is the active code page on Windows.
    const std::size_t n = std::char_traits<char>::length(raw.get());
    std::filesystem::path path(std::u8string(reinterpret_cast<const char8_t*>(raw.get()), n));
    if (path.empty()) {
        return std::unexpected(std::make_error_code(std::errc::io_error));
    }
#if defined(__ANDROID__)
    path = android_user_data_directory(path);
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        return std::unexpected(ec);
    }
#endif
    return path;
}

#endif

}

std::expected<std::filesystem::path, std::error_code> user_data_directory(
        std::string_view organization, std::string_view application) {
    if (const std::error_code org = segment_error(organization)) {
        return std::unexpected(org);
    }
    if (const std::error_code app = segment_error(application)) {
        return std::unexpected(app);
    }
#if defined(ENGINE_WITH_WINDOW) && ENGINE_WITH_WINDOW
    return user_data_directory_from_sdl(organization, application);
#else
    return std::unexpected(std::make_error_code(std::errc::function_not_supported));
#endif
}

}
