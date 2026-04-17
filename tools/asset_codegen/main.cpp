#include <engine/builtin_ids.h>
#include <engine/resources/component_codegen.h>
#include <engine/resources/meta.h>

#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    if (argc >= 3 && std::string_view(argv[1]) == "--components") {
        const std::filesystem::path output_file = argv[2];
        std::vector<std::filesystem::path> dirs;
        for (int i = 3; i < argc; ++i) {
            dirs.emplace_back(argv[i]);
        }
        const auto result = engine::component_codegen_write(dirs, output_file);
        if (!result) {
            std::cerr << result.error().message << '\n';
            return 1;
        }
        return 0;
    }

    if (argc < 3) {
        std::cerr << "usage: asset_codegen <assets_dir> <output_dir> [--engine]\n"
                  << "       asset_codegen --components <output_file> [<source_dir>...]\n";
        return 2;
    }

    const bool engine_builtins = argc >= 4 && std::string_view(argv[3]) == "--engine";
    const std::span<const engine::AssetId> reserved =
            engine_builtins ? std::span<const engine::AssetId>{} : engine::builtin::reserved();
    const auto result = engine::codegen_write(argv[1], argv[2], reserved);
    if (!result) {
        std::cerr << result.error().message << '\n';
        return 1;
    }
    return 0;
}
