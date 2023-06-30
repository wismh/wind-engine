#include <engine/builtin_ids.h>
#include <engine/resources/meta.h>

#include <iostream>
#include <span>
#include <string_view>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: asset_codegen <assets_dir> <output_dir> [--engine]\n";
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
