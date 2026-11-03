#include <gtest/gtest.h>

#include <engine/core/compression.h>
#include <engine/core/job_system.h>

#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

[[nodiscard]] Bytes text_bytes(std::size_t size) {
    constexpr std::string_view kText = "the quick brown fox jumps over the lazy dog; ";
    Bytes out(size);
    for (std::size_t i = 0; i < size; ++i) {
        out[i] = static_cast<std::uint8_t>(kText[i % kText.size()]);
    }
    return out;
}

// Runs of a few block ids, the shape of a voxel chunk: long air, stone, a little ore.
[[nodiscard]] Bytes voxel_bytes(std::size_t size, std::uint32_t seed) {
    std::mt19937 random(seed);
    Bytes out;
    out.reserve(size);
    while (out.size() < size) {
        const auto block = static_cast<std::uint8_t>(random() % 4);
        const std::size_t run = 16 + random() % 512;
        for (std::size_t i = 0; i < run && out.size() < size; ++i) {
            out.push_back(block);
        }
    }
    return out;
}

[[nodiscard]] Bytes random_bytes(std::size_t size, std::uint32_t seed) {
    std::mt19937 random(seed);
    Bytes out(size);
    for (std::uint8_t& byte : out) {
        byte = static_cast<std::uint8_t>(random());
    }
    return out;
}

void set_size(Bytes& packed, std::uint32_t size) {
    packed[1] = static_cast<std::uint8_t>(size);
    packed[2] = static_cast<std::uint8_t>(size >> 8);
    packed[3] = static_cast<std::uint8_t>(size >> 16);
    packed[4] = static_cast<std::uint8_t>(size >> 24);
}

} // namespace

TEST(Compression, RoundTripsText) {
    const Bytes input = text_bytes(1024);
    const Bytes packed = engine::compress(input);
    EXPECT_LT(packed.size(), input.size());
    EXPECT_EQ(packed[0], 1);
    const auto unpacked = engine::decompress(packed, input.size());
    ASSERT_TRUE(unpacked.has_value());
    EXPECT_EQ(*unpacked, input);
}

TEST(Compression, EmptyRoundTrips) {
    const Bytes packed = engine::compress({});
    EXPECT_EQ(packed, (Bytes{0, 0, 0, 0, 0}));
    const auto unpacked = engine::decompress(packed, 0);
    ASSERT_TRUE(unpacked.has_value());
    EXPECT_TRUE(unpacked->empty());
}

TEST(Compression, LargeRepetitiveRoundTrips) {
    const Bytes input = voxel_bytes(4u << 20, 7);
    const Bytes packed = engine::compress(input);
    EXPECT_LT(packed.size(), input.size() / 10);
    const auto unpacked = engine::decompress(packed, input.size());
    ASSERT_TRUE(unpacked.has_value());
    EXPECT_EQ(*unpacked, input);
}

TEST(Compression, IncompressibleIsStored) {
    const Bytes input = random_bytes(256u << 10, 11);
    const Bytes packed = engine::compress(input);
    EXPECT_EQ(packed.size(), input.size() + engine::kCompressionHeaderSize);
    EXPECT_EQ(packed[0], 0);
    const auto unpacked = engine::decompress(packed, input.size());
    ASSERT_TRUE(unpacked.has_value());
    EXPECT_EQ(*unpacked, input);
}

TEST(Compression, HighRoundTripsAndIsNotLarger) {
    const Bytes input = voxel_bytes(1u << 20, 3);
    const Bytes fast = engine::compress(input, engine::CompressionLevel::Fast);
    const Bytes high = engine::compress(input, engine::CompressionLevel::High);
    EXPECT_LE(high.size(), fast.size());
    EXPECT_EQ(high[0], 1);
    const auto unpacked = engine::decompress(high, input.size());
    ASSERT_TRUE(unpacked.has_value());
    EXPECT_EQ(*unpacked, input);
}

TEST(Compression, MaxSizeGuard) {
    const Bytes input = text_bytes(4096);
    const Bytes packed = engine::compress(input);
    const auto refused = engine::decompress(packed, input.size() - 1);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error(), engine::CompressionError::TooLarge);
    EXPECT_TRUE(engine::decompress(packed, input.size()).has_value());

    // A bomb: a tiny payload whose header claims almost 2 GiB is refused before anything is allocated.
    const Bytes bomb{1, 0xFF, 0xFF, 0xFF, 0x7F, 0x1F, 0x00, 0x01};
    const auto bombed = engine::decompress(bomb, 1u << 20);
    ASSERT_FALSE(bombed.has_value());
    EXPECT_EQ(bombed.error(), engine::CompressionError::TooLarge);

    const Bytes stored_bomb{0, 0xFF, 0xFF, 0xFF, 0xFF};
    const auto stored_bombed = engine::decompress(stored_bomb, 1u << 20);
    ASSERT_FALSE(stored_bombed.has_value());
    EXPECT_EQ(stored_bombed.error(), engine::CompressionError::TooLarge);
}

TEST(Compression, TruncatedHeader) {
    const Bytes packed = engine::compress(text_bytes(256));
    for (std::size_t length = 0; length < engine::kCompressionHeaderSize; ++length) {
        const auto result = engine::decompress(std::span(packed).first(length), 1u << 20);
        ASSERT_FALSE(result.has_value()) << length;
        EXPECT_EQ(result.error(), engine::CompressionError::Truncated) << length;
    }
}

TEST(Compression, TruncatedPayload) {
    const Bytes lz4 = engine::compress(text_bytes(4096));
    ASSERT_EQ(lz4[0], 1);
    const auto lz4_result = engine::decompress(std::span(lz4).first(lz4.size() - 1), 1u << 20);
    ASSERT_FALSE(lz4_result.has_value());
    EXPECT_EQ(lz4_result.error(), engine::CompressionError::Corrupt);

    const Bytes stored = engine::compress(random_bytes(512, 5));
    ASSERT_EQ(stored[0], 0);
    const auto stored_result = engine::decompress(std::span(stored).first(stored.size() - 1), 1u << 20);
    ASSERT_FALSE(stored_result.has_value());
    EXPECT_EQ(stored_result.error(), engine::CompressionError::Corrupt);

    // The header alone, claiming bytes that are not there.
    const auto header_only = engine::decompress(std::span(lz4).first(engine::kCompressionHeaderSize), 1u << 20);
    ASSERT_FALSE(header_only.has_value());
    EXPECT_EQ(header_only.error(), engine::CompressionError::Corrupt);
}

TEST(Compression, StoredTrailingBytes) {
    Bytes stored = engine::compress(random_bytes(512, 5));
    ASSERT_EQ(stored[0], 0);
    stored.push_back(0);
    const auto result = engine::decompress(stored, 1u << 20);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), engine::CompressionError::Corrupt);
}

TEST(Compression, UnknownFormat) {
    Bytes packed = engine::compress(text_bytes(256));
    packed[0] = 7;
    const auto result = engine::decompress(packed, 1u << 20);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), engine::CompressionError::UnknownFormat);
}

TEST(Compression, SizeHeaderMismatch) {
    const Bytes input = text_bytes(4096);
    const Bytes packed = engine::compress(input);
    ASSERT_EQ(packed[0], 1);
    for (const std::uint32_t size : {4095u, 4097u}) {
        Bytes edited = packed;
        set_size(edited, size);
        const auto result = engine::decompress(edited, 1u << 20);
        ASSERT_FALSE(result.has_value()) << size;
        EXPECT_EQ(result.error(), engine::CompressionError::Corrupt) << size;
    }
}

TEST(Compression, CorruptPayload) {
    // Token 0x00 (no literals, a 4-byte match) at offset 5, before the start of the output.
    const Bytes before_start{1, 100, 0, 0, 0, 0x00, 0x05, 0x00};
    const auto result = engine::decompress(before_start, 1u << 20);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), engine::CompressionError::Corrupt);
}

TEST(Compression, GarbageStaysBounded) {
    std::mt19937 random(13);
    for (int round = 0; round < 2000; ++round) {
        const std::uint32_t size = random() % 4097;
        Bytes garbage = random_bytes(1 + random() % 512, static_cast<std::uint32_t>(round));
        garbage.insert(garbage.begin(), engine::kCompressionHeaderSize, 0);
        garbage[0] = 1;
        set_size(garbage, size);
        const auto result = engine::decompress(garbage, 4096);
        if (result.has_value()) {
            EXPECT_EQ(result->size(), size) << round;
        }
    }
}

TEST(Compression, CallableFromWorkerThreads) {
    constexpr std::size_t kCount = 64;
    std::vector<Bytes> inputs;
    for (std::size_t i = 0; i < kCount; ++i) {
        inputs.push_back(voxel_bytes(32u << 10, static_cast<std::uint32_t>(i)));
    }
    std::vector<Bytes> serial;
    for (const Bytes& input : inputs) {
        serial.push_back(engine::compress(input, engine::CompressionLevel::High));
    }

    std::vector<Bytes> fast(kCount);
    std::vector<Bytes> high(kCount);
    std::vector<int> round_trips(kCount, 0);
    engine::JobSystem jobs(4);
    jobs.parallel_for(kCount, 1, [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) {
            fast[i] = engine::compress(inputs[i], engine::CompressionLevel::Fast);
            high[i] = engine::compress(inputs[i], engine::CompressionLevel::High);
            const auto from_fast = engine::decompress(fast[i], inputs[i].size());
            const auto from_high = engine::decompress(high[i], inputs[i].size());
            round_trips[i] = static_cast<int>(from_fast.has_value() && *from_fast == inputs[i]) +
                             static_cast<int>(from_high.has_value() && *from_high == inputs[i]);
        }
    });
    jobs.stop();
    for (std::size_t i = 0; i < kCount; ++i) {
        EXPECT_EQ(round_trips[i], 2) << i;
        EXPECT_EQ(high[i], serial[i]) << i;
    }
}

TEST(Compression, ErrorNames) {
    EXPECT_EQ(engine::to_string(engine::CompressionError::Truncated), "Truncated");
    EXPECT_EQ(engine::to_string(engine::CompressionError::UnknownFormat), "UnknownFormat");
    EXPECT_EQ(engine::to_string(engine::CompressionError::TooLarge), "TooLarge");
    EXPECT_EQ(engine::to_string(engine::CompressionError::Corrupt), "Corrupt");
}
