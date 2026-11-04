#include <gtest/gtest.h>

#include "net/payload_assembler.h"

#include <engine/core/compression.h>
#include <engine/net/byte_stream.h>
#include <engine/net/net_messages.h>

#include <cstdint>
#include <span>
#include <vector>

namespace engine::net {
namespace {

constexpr MessageId kChunk = 0x2000;
constexpr std::size_t kLimit = 1024;

std::vector<std::uint8_t> begin_bytes(PayloadId id, MessageId message, std::uint8_t flags, std::uint32_t total,
        std::span<const std::uint8_t> data = {}) {
    ByteWriter out;
    out.write_u32(id);
    out.write_u16(message);
    out.write_u8(flags);
    out.write_u32(total);
    out.append(data);
    return out.buffer();
}

std::vector<std::uint8_t> fragment_bytes(PayloadId id, std::uint32_t offset, std::span<const std::uint8_t> data) {
    ByteWriter out;
    out.write_u32(id);
    out.write_u32(offset);
    out.append(data);
    return out.buffer();
}

PayloadAssembler::Result begin(PayloadAssembler& assembler, std::span<const std::uint8_t> bytes) {
    ByteReader reader(bytes);
    return assembler.begin(reader);
}

PayloadAssembler::Result fragment(PayloadAssembler& assembler, std::span<const std::uint8_t> bytes) {
    ByteReader reader(bytes);
    return assembler.fragment(reader);
}

PayloadAssembler make() {
    PayloadAssembler assembler;
    assembler.set_limit(kLimit);
    return assembler;
}

TEST(PayloadAssembler, ReassemblesInterleavedPayloads) {
    PayloadAssembler assembler = make();
    const std::vector<std::uint8_t> a{1, 2, 3, 4};
    const std::vector<std::uint8_t> b{9, 8};

    EXPECT_FALSE(begin(assembler, begin_bytes(1, kChunk, 0, 4, std::span(a).first(2))).done.has_value());
    EXPECT_FALSE(begin(assembler, begin_bytes(2, kChunk + 1, 0, 2, std::span(b).first(1))).done.has_value());
    EXPECT_EQ(assembler.open_count(), 2u);
    EXPECT_EQ(assembler.held_bytes(), 3u);

    const auto second = fragment(assembler, fragment_bytes(2, 1, std::span(b).subspan(1)));
    ASSERT_TRUE(second.done.has_value());
    EXPECT_EQ(second.done->id, 2u);
    EXPECT_EQ(second.done->message, (std::vector<std::uint8_t>{0x01, 0x20, 9, 8}));

    const auto first = fragment(assembler, fragment_bytes(1, 2, std::span(a).subspan(2)));
    ASSERT_TRUE(first.done.has_value());
    EXPECT_EQ(first.done->message, (std::vector<std::uint8_t>{0x00, 0x20, 1, 2, 3, 4}));
    EXPECT_EQ(assembler.open_count(), 0u);
    EXPECT_EQ(assembler.held_bytes(), 0u);
}

TEST(PayloadAssembler, EmptyPayloadFinishesAtBegin) {
    PayloadAssembler assembler = make();
    const auto result = begin(assembler, begin_bytes(1, kChunk, 0, 0));
    ASSERT_TRUE(result.done.has_value());
    EXPECT_EQ(result.done->message, (std::vector<std::uint8_t>{0x00, 0x20}));
}

TEST(PayloadAssembler, CompressedIsDecompressed) {
    PayloadAssembler assembler = make();
    const std::vector<std::uint8_t> raw(512, 7);
    const auto packed = compress(raw);
    ASSERT_LT(packed.size(), raw.size());
    const auto result = begin(
            assembler, begin_bytes(1, kChunk, 1, static_cast<std::uint32_t>(packed.size()), packed));
    ASSERT_TRUE(result.done.has_value());
    ASSERT_EQ(result.done->message.size(), raw.size() + 2u);
    EXPECT_EQ(result.done->message[2], 7);
}

TEST(PayloadAssembler, DecompressionOverTheLimitIsRefused) {
    PayloadAssembler assembler = make();
    const std::vector<std::uint8_t> raw(kLimit + 1, 7);
    const auto packed = compress(raw);
    ASSERT_LE(packed.size(), kLimit);
    EXPECT_TRUE(begin(assembler, begin_bytes(1, kChunk, 1, static_cast<std::uint32_t>(packed.size()), packed))
                    .error.has_value());
}

TEST(PayloadAssembler, CorruptCompressedBytesAreRefused) {
    PayloadAssembler assembler = make();
    const std::vector<std::uint8_t> garbage{9, 9, 9, 9, 9, 9, 9, 9};
    EXPECT_TRUE(begin(assembler, begin_bytes(1, kChunk, 1, 8, garbage)).error.has_value());
}

TEST(PayloadAssembler, CancelFreesAndUnknownCancelIsNormal) {
    PayloadAssembler assembler = make();
    const std::vector<std::uint8_t> data{1, 2};
    (void)begin(assembler, begin_bytes(1, kChunk, 0, 10, data));
    EXPECT_EQ(assembler.held_bytes(), 2u);

    ByteWriter cancel;
    cancel.write_u32(1);
    ByteReader reader(cancel.bytes());
    EXPECT_FALSE(assembler.cancel(reader).error.has_value());
    EXPECT_EQ(assembler.open_count(), 0u);
    EXPECT_EQ(assembler.held_bytes(), 0u);

    ByteReader again(cancel.bytes());
    EXPECT_FALSE(assembler.cancel(again).error.has_value());

    ByteReader truncated(std::span(cancel.bytes()).first(2));
    EXPECT_TRUE(assembler.cancel(truncated).error.has_value());
}

TEST(PayloadAssembler, MemoryFollowsBytesNotClaims) {
    PayloadAssembler assembler = make();
    (void)begin(assembler, begin_bytes(1, kChunk, 0, kLimit));
    EXPECT_EQ(assembler.held_bytes(), 0u);
    assembler.clear();
    EXPECT_EQ(assembler.open_count(), 0u);
}

TEST(PayloadAssembler, MalformedBeginsAreRefused) {
    const std::vector<std::uint8_t> data{1};
    struct Case {
        const char* name;
        std::vector<std::uint8_t> bytes;
    };
    const std::vector<Case> cases{
            {"no id", begin_bytes(kNoPayload, kChunk, 0, 1)},
            {"invalid message", begin_bytes(1, kInvalidMessageId, 0, 1)},
            {"engine message", begin_bytes(1, kSnapshotMessageId, 0, 1)},
            {"unknown flags", begin_bytes(1, kChunk, 0x80, 1)},
            {"over the limit", begin_bytes(1, kChunk, 0, kLimit + 1)},
            {"bytes beyond the size", begin_bytes(1, kChunk, 0, 0, data)},
    };
    for (const Case& test : cases) {
        PayloadAssembler assembler = make();
        EXPECT_TRUE(begin(assembler, test.bytes).error.has_value()) << test.name;
    }
    const auto whole = begin_bytes(1, kChunk, 0, 1);
    for (std::size_t size = 0; size < whole.size(); ++size) {
        PayloadAssembler assembler = make();
        EXPECT_TRUE(begin(assembler, std::span(whole).first(size)).error.has_value()) << size;
    }
}

TEST(PayloadAssembler, MalformedFragmentsAreRefused) {
    const std::vector<std::uint8_t> data{1, 2};
    PayloadAssembler assembler = make();
    (void)begin(assembler, begin_bytes(1, kChunk, 0, 3, std::span(data).first(1)));

    EXPECT_TRUE(fragment(assembler, fragment_bytes(7, 0, data)).error.has_value()) << "unknown id";
    EXPECT_TRUE(fragment(assembler, fragment_bytes(1, 5, data)).error.has_value()) << "gap";
    EXPECT_TRUE(fragment(assembler, fragment_bytes(1, 0, data)).error.has_value()) << "overlap";
    const std::vector<std::uint8_t> too_many{1, 2, 3};
    EXPECT_TRUE(fragment(assembler, fragment_bytes(1, 1, too_many)).error.has_value()) << "beyond the size";
    const auto whole = fragment_bytes(1, 1, data);
    for (std::size_t size = 0; size < 8; ++size) {
        EXPECT_TRUE(fragment(assembler, std::span(whole).first(size)).error.has_value()) << size;
    }
    EXPECT_TRUE(begin(assembler, begin_bytes(1, kChunk, 0, 3)).error.has_value()) << "duplicate begin";
}

TEST(PayloadAssembler, HeldBytesOverTheLimitAreRefused) {
    PayloadAssembler assembler = make();
    const std::vector<std::uint8_t> big(kLimit - 1, 1);
    const std::vector<std::uint8_t> two{1, 2};
    EXPECT_FALSE(begin(assembler, begin_bytes(1, kChunk, 0, kLimit, big)).error.has_value());
    EXPECT_TRUE(begin(assembler, begin_bytes(2, kChunk, 0, 10, two)).error.has_value());
}

TEST(PayloadAssembler, TooManyOpenAreRefused) {
    PayloadAssembler assembler = make();
    for (PayloadId id = 1; id <= PayloadAssembler::kMaxOpen; ++id) {
        ASSERT_FALSE(begin(assembler, begin_bytes(id, kChunk, 0, 1)).error.has_value());
    }
    EXPECT_TRUE(begin(assembler, begin_bytes(PayloadAssembler::kMaxOpen + 1, kChunk, 0, 1)).error.has_value());
}

} // namespace
} // namespace engine::net
