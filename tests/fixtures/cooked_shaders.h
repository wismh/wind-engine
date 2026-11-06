#pragma once

#include <engine/resources/shader_cook.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

// Replaces a source `.shader` with the cooked blob `load_gpu` reads. The cooker must already be registered.
inline bool cook_shader_in_place(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) {
        return false;
    }
    const std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    auto cooked = engine::cook_shader_asset(xml, file.parent_path());
    if (!cooked) {
        return false;
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(cooked->data()), static_cast<std::streamsize>(cooked->size()));
    return static_cast<bool>(out);
}

// One cooked copy of builtin_assets for the process. Source XML stays in ENGINE_BUILTIN_ASSETS_DIR.
inline std::filesystem::path cooked_builtin_assets() {
    static const std::filesystem::path cached = [] {
        const std::filesystem::path src{ENGINE_BUILTIN_ASSETS_DIR};
        const std::filesystem::path dest = std::filesystem::temp_directory_path() / "wind_builtin_cooked";
        std::error_code ec;
        std::filesystem::remove_all(dest, ec);
        std::filesystem::copy(src, dest, std::filesystem::copy_options::recursive, ec);
        if (ec) {
            return std::filesystem::path{};
        }
        for (const std::filesystem::directory_entry& entry :
                std::filesystem::directory_iterator(dest / "shaders", ec)) {
            if (ec || entry.path().extension() != ".shader") {
                continue;
            }
            if (!cook_shader_in_place(entry.path())) {
                return std::filesystem::path{};
            }
        }
        return dest;
    }();
    return cached;
}
