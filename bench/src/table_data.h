#pragma once

#include "bench_random.h"
#include "table_record.h"

#include <cstddef>
#include <vector>

namespace bench {

// Rows of the table scene.
inline constexpr std::size_t kTableRows = 10000;

// The record with this id, drawn from `random`: a unit name, a value in 0..99999, one of five statuses, and a ratio
// with three decimals.
[[nodiscard]] TableRecord make_table_record(int id, BenchRandom& random);

// `count` records with ids 1..count, from kBenchSeed unless told otherwise.
[[nodiscard]] std::vector<TableRecord> make_table_records(std::size_t count, std::uint64_t seed = kBenchSeed);

}
