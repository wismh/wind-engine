#include "resources/icon_codegen.h"

#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: icon_codegen <input.png> <output_dir>\n";
        return 2;
    }

    const auto result = engine::icon_codegen_write(argv[1], argv[2]);
    if (!result) {
        std::cerr << result.error().message << '\n';
        return 1;
    }
    return 0;
}
