#include "text_data.h"

#include <array>
#include <cctype>
#include <string_view>

namespace bench {
namespace {

constexpr std::array<std::string_view, 32> kWords{
        "river",   "stone",  "harbor", "lantern", "meadow", "copper", "signal", "winter",
        "garden",  "engine", "quiet",  "orchard", "pillar", "window", "silver", "market",
        "thunder", "ledger", "bridge", "candle",  "forest", "anchor", "valley", "canvas",
        "measure", "frame",  "glyph",  "layout",  "paint",  "border", "scroll", "kernel",
};

}

std::string make_paragraph(BenchRandom& random, int words) {
    std::string paragraph;
    for (int i = 0; i < words; ++i) {
        if (i > 0) {
            paragraph += ' ';
        }
        paragraph += kWords[static_cast<std::size_t>(random.between(0, static_cast<int>(kWords.size()) - 1))];
    }
    if (!paragraph.empty()) {
        paragraph[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(paragraph[0])));
        paragraph += '.';
    }
    return paragraph;
}

std::vector<std::string> make_paragraphs(std::size_t count, int words, std::uint64_t seed) {
    BenchRandom random(seed);
    std::vector<std::string> paragraphs;
    paragraphs.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        paragraphs.push_back(make_paragraph(random, words));
    }
    return paragraphs;
}

}
