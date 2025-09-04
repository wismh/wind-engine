#include "table_data.h"

#include <array>
#include <format>
#include <string_view>

namespace bench {
namespace {

constexpr std::array<std::string_view, 12> kKinds{
        "Archer", "Knight", "Pikeman", "Scout", "Mage", "Healer",
        "Catapult", "Ranger", "Paladin", "Worker", "Rider", "Sentry",
};

constexpr std::array<std::string_view, 5> kStatuses{"Idle", "Moving", "Fighting", "Building", "Wounded"};

template<std::size_t N>
std::string_view pick(const std::array<std::string_view, N>& words, BenchRandom& random) {
    return words[static_cast<std::size_t>(random.between(0, static_cast<int>(N) - 1))];
}

}

TableRecord make_table_record(int id, BenchRandom& random) {
    TableRecord record;
    record.id = id;
    record.id_text = std::format("{:05}", id);
    const std::string_view kind = pick(kKinds, random);
    record.name = std::format("{} {:03}", kind, random.between(1, 999));
    record.value = std::to_string(random.between(0, 99999));
    record.status = std::string(pick(kStatuses, random));
    record.ratio = std::format("{:.3f}", random.unit());
    return record;
}

std::vector<TableRecord> make_table_records(std::size_t count, std::uint64_t seed) {
    BenchRandom random(seed);
    std::vector<TableRecord> records;
    records.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        records.push_back(make_table_record(static_cast<int>(i) + 1, random));
    }
    return records;
}

}
