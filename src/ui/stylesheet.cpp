#include <engine/ui/stylesheet.h>

namespace engine::ui {

std::uint64_t next_stylesheet_generation() noexcept {
    static std::uint64_t counter = 0;
    return ++counter;
}

}
