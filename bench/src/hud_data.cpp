#include "hud_data.h"

namespace bench {

std::vector<UnitMarker> make_unit_markers(std::size_t count, glm::vec2 world, std::uint64_t seed) {
    BenchRandom random(seed);
    std::vector<UnitMarker> units;
    units.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        UnitMarker unit;
        unit.position = {static_cast<float>(random.unit()) * world.x, static_cast<float>(random.unit()) * world.y};
        unit.team = random.between(0, kTeams - 1);
        units.push_back(unit);
    }
    return units;
}

}
