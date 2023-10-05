#include <engine/builtin_ids.h>
#include <engine/resources/meta.h>

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: asset_codegen <assets_dir> <output_dir>\n";
        return 2;
    }

    const auto result = engine::codegen_write(argv[1], argv[2], engine::builtin::reserved());
    if (!result) {
        std::cerr << result.error().message << '\n';
        return 1;
    }
    return 0;
}
