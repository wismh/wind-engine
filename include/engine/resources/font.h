#pragma once

// docs/tech/modules/Resources.md

#include <cstdint>
#include <vector>

namespace engine {

struct Font {
    std::vector<std::uint8_t> bytes;
};

}
