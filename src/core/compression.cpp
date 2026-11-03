#include <engine/core/compression.h>

#include <lz4.h>
#include <lz4hc.h>

#include <algorithm>
#include <stdexcept>

namespace engine {

namespace {

constexpr std::uint8_t kFormatStored = 0;
constexpr std::uint8_t kFormatLz4 = 1;

static_assert(kCompressionMaxInput == LZ4_MAX_INPUT_SIZE);

void write_header(std::vector<std::uint8_t>& out, std::uint8_t format, std::size_t size) {
    const auto value = static_cast<std::uint32_t>(size);
    out[0] = format;
    out[1] = static_cast<std::uint8_t>(value);
    out[2] = static_cast<std::uint8_t>(value >> 8);
    out[3] = static_cast<std::uint8_t>(value >> 16);
    out[4] = static_cast<std::uint8_t>(value >> 24);
}

std::vector<std::uint8_t> stored(std::span<const std::uint8_t> input) {
    std::vector<std::uint8_t> out(kCompressionHeaderSize + input.size());
    write_header(out, kFormatStored, input.size());
    if (!input.empty()) {
        std::copy(input.begin(), input.end(), out.begin() + kCompressionHeaderSize);
    }
    return out;
}

} // namespace

std::vector<std::uint8_t> compress(std::span<const std::uint8_t> input, CompressionLevel level) {
    if (input.size() > kCompressionMaxInput) {
        throw std::length_error("engine::compress: input above kCompressionMaxInput");
    }
    if (input.empty()) {
        return stored(input);
    }
    const int source_size = static_cast<int>(input.size());
    const int bound = LZ4_compressBound(source_size);
    std::vector<std::uint8_t> out(kCompressionHeaderSize + static_cast<std::size_t>(bound));
    const auto* source = reinterpret_cast<const char*>(input.data());
    auto* destination = reinterpret_cast<char*>(out.data() + kCompressionHeaderSize);
    const int written = level == CompressionLevel::High
                            ? LZ4_compress_HC(source, destination, source_size, bound, LZ4HC_CLEVEL_DEFAULT)
                            : LZ4_compress_default(source, destination, source_size, bound);
    if (written <= 0 || static_cast<std::size_t>(written) >= input.size()) {
        return stored(input);
    }
    out.resize(kCompressionHeaderSize + static_cast<std::size_t>(written));
    write_header(out, kFormatLz4, input.size());
    return out;
}

std::expected<std::vector<std::uint8_t>, CompressionError> decompress(std::span<const std::uint8_t> input,
                                                                     std::size_t max_size) {
    if (input.size() < kCompressionHeaderSize) {
        return std::unexpected(CompressionError::Truncated);
    }
    const std::uint8_t format = input[0];
    if (format != kFormatStored && format != kFormatLz4) {
        return std::unexpected(CompressionError::UnknownFormat);
    }
    const std::size_t size = static_cast<std::size_t>(input[1]) | (static_cast<std::size_t>(input[2]) << 8) |
                             (static_cast<std::size_t>(input[3]) << 16) | (static_cast<std::size_t>(input[4]) << 24);
    if (size > max_size) {
        return std::unexpected(CompressionError::TooLarge);
    }
    const auto payload = input.subspan(kCompressionHeaderSize);
    if (format == kFormatStored) {
        if (payload.size() != size) {
            return std::unexpected(CompressionError::Corrupt);
        }
        return std::vector<std::uint8_t>(payload.begin(), payload.end());
    }
    // An LZ4 block of a non-empty input is never empty, and `compress` never writes one for empty input.
    if (size == 0 || size > kCompressionMaxInput || payload.empty() || payload.size() > kCompressionMaxInput) {
        return std::unexpected(CompressionError::Corrupt);
    }
    std::vector<std::uint8_t> out(size);
    const int read = LZ4_decompress_safe(reinterpret_cast<const char*>(payload.data()),
                                         reinterpret_cast<char*>(out.data()), static_cast<int>(payload.size()),
                                         static_cast<int>(size));
    if (read < 0 || static_cast<std::size_t>(read) != size) {
        return std::unexpected(CompressionError::Corrupt);
    }
    return out;
}

} // namespace engine
