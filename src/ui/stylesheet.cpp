#include <engine/ui/stylesheet.h>

namespace engine::ui {

std::uint64_t next_stylesheet_generation() noexcept {
    static std::uint64_t counter = 0;
    return ++counter;
}

void Stylesheet::set_environment(std::string_view name, std::string_view value) {
    const auto found = environment.find(std::string(name));
    if (found != environment.end() && found->second == value) {
        return;
    }
    environment.insert_or_assign(std::string(name), std::string(value));
    generation = next_stylesheet_generation();
}

}
