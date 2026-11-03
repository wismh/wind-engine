#pragma once

// docs/tech/modules/Core.md#compression

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace engine {

// How hard `compress` works. Both write the same format and `decompress` reads either at the same speed.
enum class CompressionLevel {
    Fast,  // LZ4 at its default speed: packets, anything per frame
    High,  // LZ4 HC level 9: region saves; several times slower to compress, smaller output
};

// Why `decompress` refused its input. The checks run in this order.
enum class CompressionError {
    Truncated,      // shorter than the header
    UnknownFormat,  // the format byte names no format this build knows
    TooLarge,       // the header's uncompressed size is above max_size; nothing was allocated or decoded
    Corrupt,        // the payload does not decode to exactly the header's uncompressed size
};

[[nodiscard]] constexpr std::string_view to_string(CompressionError error) noexcept {
    switch (error) {
        case CompressionError::Truncated:
            return "Truncated";
        case CompressionError::UnknownFormat:
            return "UnknownFormat";
        case CompressionError::TooLarge:
            return "TooLarge";
        case CompressionError::Corrupt:
            return "Corrupt";
    }
    return "Unknown";
}

// Every output of `compress` starts with this header: one format byte (0 stored, 1 LZ4 block), then the uncompressed
// size as a little-endian u32. The format is frozen: saves keep it on disk.
inline constexpr std::size_t kCompressionHeaderSize = 5;

// The largest input `compress` takes (LZ4_MAX_INPUT_SIZE, about 1.97 GiB).
inline constexpr std::size_t kCompressionMaxInput = 0x7E000000;

// Compresses `input`. Never more than `input.size() + kCompressionHeaderSize` bytes: data that does not shrink is
// stored as it is. An empty input gives the header alone. An input above `kCompressionMaxInput` is a precondition
// violation and throws `std::length_error`.
//
// Any thread: pure, no engine state. A job worker may call it (the exception to Principles #12).
[[nodiscard]] std::vector<std::uint8_t> compress(std::span<const std::uint8_t> input,
                                                 CompressionLevel level = CompressionLevel::Fast);

// Reverses `compress`. `max_size` caps the uncompressed size the header may claim; it is checked before anything is
// allocated, so a hostile header cannot make it allocate more. There is no checksum: a payload damaged in a way that
// still decodes gives different bytes, not an error. Integrity belongs to the transport or the save format.
//
// Any thread: pure, no engine state.
[[nodiscard]] std::expected<std::vector<std::uint8_t>, CompressionError> decompress(std::span<const std::uint8_t> input,
                                                                                   std::size_t max_size);

} // namespace engine
