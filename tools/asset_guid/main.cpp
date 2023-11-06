#include <engine/resources/asset_guid.h>

#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: asset_guid <assets_dir>\n";
        return 2;
    }

    const int written = engine::write_missing_metas(argv[1]);
    std::cout << "wrote " << written << " .meta file(s)\n";
    return 0;
}
