#include <gtest/gtest.h>

#include "net/input_message.h"

#include <engine/net/byte_stream.h>
#include <engine/net/net_messages.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace engine::net {
namespace {

std::optional<InputMessage> decode(std::span<const std::uint8_t> bytes) {
    ByteReader reader(bytes);
    if (reader.read_u16() != kInputMessageId) {
        return std::nullopt;
    }
    return InputMessage::decode(reader);
}

// A raw message: `newest`, then per entity its id and input sizes (each input filled with its index).
std::vector<std::uint8_t> raw(Tick newest, const std::vector<std::pair<NetId, std::vector<std::size_t>>>& entities) {
    ByteWriter out;
    out.write_u16(kInputMessageId);
    out.write_u32(newest);
    out.write_u8(static_cast<std::uint8_t>(entities.size()));
    for (const auto& [id, sizes] : entities) {
        out.write_u32(id);
        out.write_u8(static_cast<std::uint8_t>(sizes.size()));
        for (std::size_t i = 0; i < sizes.size(); ++i) {
            out.write_u16(static_cast<std::uint16_t>(sizes[i]));
            for (std::size_t b = 0; b < sizes[i]; ++b) {
                out.write_u8(static_cast<std::uint8_t>(i));
            }
        }
    }
    return out.buffer();
}

TEST(InputMessage, RoundTrip) {
    const std::vector<std::uint8_t> a{1, 2, 3};
    const std::vector<std::uint8_t> b{4};
    const std::vector<std::uint8_t> c{};
    InputMessage message{.newest = 40};
    message.entities.push_back({.id = 3, .inputs = {a, b}});
    message.entities.push_back({.id = 9, .inputs = {c}});
    const auto bytes = message.encode();
    EXPECT_EQ(bytes.size(), 2u + 4u + 1u + (4u + 1u + 2u + 3u + 2u + 1u) + (4u + 1u + 2u));

    const auto decoded = decode(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->newest, 40u);
    ASSERT_EQ(decoded->entities.size(), 2u);
    EXPECT_EQ(decoded->entities[0].id, 3u);
    ASSERT_EQ(decoded->entities[0].inputs.size(), 2u);
    EXPECT_TRUE(std::ranges::equal(decoded->entities[0].inputs[0], a));
    EXPECT_TRUE(std::ranges::equal(decoded->entities[0].inputs[1], b));
    EXPECT_EQ(decoded->entities[1].id, 9u);
    ASSERT_EQ(decoded->entities[1].inputs.size(), 1u);
    EXPECT_TRUE(decoded->entities[1].inputs[0].empty());
}

TEST(InputMessage, TruncatedAndTrailingBytesRejected) {
    const auto bytes = raw(40, {{3, {3, 1}}, {9, {2}}});
    ASSERT_TRUE(decode(bytes).has_value());
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        EXPECT_FALSE(decode(std::span(bytes).first(size)).has_value()) << size;
    }
    auto longer = bytes;
    longer.push_back(0);
    EXPECT_FALSE(decode(longer).has_value());
}

TEST(InputMessage, CapsRejected) {
    EXPECT_FALSE(decode(raw(kNoTick, {{3, {1}}})).has_value());
    EXPECT_FALSE(decode(raw(40, {})).has_value());
    EXPECT_FALSE(decode(raw(40, {{kInvalidNetId, {1}}})).has_value());
    EXPECT_FALSE(decode(raw(40, {{3, {}}})).has_value());

    std::vector<std::pair<NetId, std::vector<std::size_t>>> many;
    for (NetId id = 1; id <= kMaxInputEntities; ++id) {
        many.push_back({id, {1}});
    }
    EXPECT_TRUE(decode(raw(40, many)).has_value());
    many.push_back({99, {1}});
    EXPECT_FALSE(decode(raw(40, many)).has_value());

    EXPECT_TRUE(decode(raw(40, {{3, std::vector<std::size_t>(kMaxInputsPerMessage, 1)}})).has_value());
    EXPECT_FALSE(decode(raw(40, {{3, std::vector<std::size_t>(kMaxInputsPerMessage + 1, 1)}})).has_value());
    // More inputs than ticks before `newest`.
    EXPECT_TRUE(decode(raw(3, {{3, {1, 1, 1}}})).has_value());
    EXPECT_FALSE(decode(raw(3, {{3, {1, 1, 1, 1}}})).has_value());

    EXPECT_TRUE(decode(raw(40, {{3, {kMaxInputBytes}}})).has_value());
    EXPECT_FALSE(decode(raw(40, {{3, {kMaxInputBytes + 1}}})).has_value());
}

} // namespace
} // namespace engine::net
