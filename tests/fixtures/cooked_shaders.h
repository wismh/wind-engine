#pragma once

#include <engine/resources/shader_cook.h>
#include "shader_cook/cook.h"

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fixtures {

inline std::uint32_t current_process_id() {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(_getpid());
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
}

inline std::string unique_process_suffix() {
    static std::atomic<std::uint32_t> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(current_process_id()) + "_" + std::to_string(stamp) + "_" + std::to_string(++counter);
}

struct CookedAssetsDirectory {
    std::filesystem::path path_;

    ~CookedAssetsDirectory() {
        if (!path_.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(path_, ec);
        }
    }
};

} // namespace fixtures

// Replaces a source `.shader` with the cooked blob `load_gpu` reads.
inline bool cook_shader_in_place(const std::filesystem::path& file) {
    engine::shader_cook::register_shader_cooker();
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
inline const std::filesystem::path& cooked_builtin_assets() {
    static const fixtures::CookedAssetsDirectory cached = [] {
        engine::shader_cook::register_shader_cooker();
        const std::filesystem::path src{ENGINE_BUILTIN_ASSETS_DIR};
        const std::filesystem::path dest =
                std::filesystem::temp_directory_path() / ("wind_builtin_cooked_" + fixtures::unique_process_suffix());
        std::error_code ec;
        std::filesystem::remove_all(dest, ec);
        std::filesystem::copy(src, dest, std::filesystem::copy_options::recursive, ec);
        if (ec) {
            return fixtures::CookedAssetsDirectory{};
        }
        for (const std::filesystem::directory_entry& entry :
                std::filesystem::directory_iterator(dest / "shaders", ec)) {
            if (ec || entry.path().extension() != ".shader") {
                continue;
            }
            if (!cook_shader_in_place(entry.path())) {
                std::error_code clean_ec;
                std::filesystem::remove_all(dest, clean_ec);
                return fixtures::CookedAssetsDirectory{};
            }
        }
        return fixtures::CookedAssetsDirectory{dest};
    }();
    return cached.path_;
}
