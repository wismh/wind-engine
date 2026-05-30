#pragma once

// docs/tech/features/Assets.md

#include <string>

namespace engine {

// An `importer = "data"` file as it is on disk. The engine does not parse it.
struct TextAsset {
    std::string text;
};

}
