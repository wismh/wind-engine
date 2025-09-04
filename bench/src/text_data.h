#pragma once

#include "bench_random.h"

#include <cstddef>
#include <string>
#include <vector>

namespace bench {

// `words` words from a fixed word list, the first capitalised, ending with a period.
[[nodiscard]] std::string make_paragraph(BenchRandom& random, int words);

// `count` paragraphs of `words` words each, from one generator seeded with `seed`.
[[nodiscard]] std::vector<std::string> make_paragraphs(std::size_t count, int words, std::uint64_t seed = kBenchSeed);

}
