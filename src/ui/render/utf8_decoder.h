#pragma once

#include <cstdint>

namespace engine::ui {

// Byte-at-a-time UTF-8 decoder: Bjoern Hoehrmann's DFA, the one fontstash runs (`fons__decutf8`). Text layout must
// walk strings exactly as NanoVG did, including its quirk that a malformed sequence parks the decoder in the reject
// state for the rest of the string, so every byte after it decodes to nothing.
struct Utf8Decoder {
    static constexpr std::uint32_t kAccept = 0;
    static constexpr std::uint32_t kReject = 12;

    std::uint32_t state = kAccept;
    std::uint32_t codepoint = 0;

    // Feeds one byte. Returns the new state: `kAccept` means `codepoint` is a complete code point.
    std::uint32_t step(unsigned char byte) noexcept;
};

}
