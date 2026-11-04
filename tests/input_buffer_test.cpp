#include <gtest/gtest.h>

#include "net/input_buffer.h"

#include <engine/net/net_messages.h>

#include <cstdint>
#include <vector>

namespace engine::net {
namespace {

std::vector<std::uint8_t> input(std::uint8_t value) {
    return {value};
}

[[nodiscard]] int value(const InputBuffer::Taken& taken) {
    return taken.bytes.empty() ? -1 : taken.bytes[0];
}

TEST(InputBuffer, TakesEachTickOnceInOrder) {
    InputBuffer buffer(10);
    EXPECT_EQ(buffer.insert(13, input(3), 10), InputBuffer::Insert::Stored);
    EXPECT_EQ(buffer.insert(11, input(1), 10), InputBuffer::Insert::Stored);
    EXPECT_EQ(buffer.insert(12, input(2), 10), InputBuffer::Insert::Stored);
    EXPECT_EQ(buffer.newest(), 13u);

    for (Tick tick = 11; tick <= 13; ++tick) {
        const auto taken = buffer.take(tick);
        EXPECT_TRUE(taken.real);
        EXPECT_FALSE(taken.repeated);
        EXPECT_EQ(value(taken), static_cast<int>(tick - 10));
    }
    EXPECT_EQ(buffer.pending(), 0u);
}

TEST(InputBuffer, FirstCopyWins) {
    InputBuffer buffer(0);
    EXPECT_EQ(buffer.insert(1, input(1), 0), InputBuffer::Insert::Stored);
    EXPECT_EQ(buffer.insert(1, input(9), 0), InputBuffer::Insert::Duplicate);
    EXPECT_EQ(value(buffer.take(1)), 1);
    // Applied already: still a duplicate, not late.
    EXPECT_EQ(buffer.insert(1, input(9), 1), InputBuffer::Insert::Duplicate);
}

TEST(InputBuffer, MissingTickRepeatsTheLastInput) {
    InputBuffer buffer(0);
    const auto before = buffer.take(1);
    EXPECT_FALSE(before.real);
    EXPECT_FALSE(before.repeated);
    EXPECT_TRUE(before.bytes.empty());

    ASSERT_EQ(buffer.insert(2, input(5), 1), InputBuffer::Insert::Stored);
    EXPECT_TRUE(buffer.take(2).real);
    const auto repeat = buffer.take(3);
    EXPECT_FALSE(repeat.real);
    EXPECT_TRUE(repeat.repeated);
    EXPECT_EQ(value(repeat), 5);
}

TEST(InputBuffer, InputForATickThatRanIsLate) {
    InputBuffer buffer(0);
    (void)buffer.take(1);
    EXPECT_EQ(buffer.insert(1, input(1), 1), InputBuffer::Insert::Late);
    EXPECT_EQ(buffer.insert(0, input(1), 1), InputBuffer::Insert::Late);
    // Older than the 64 ticks it remembers: late as well.
    for (Tick tick = 2; tick <= 80; ++tick) {
        ASSERT_EQ(buffer.insert(tick, input(1), tick - 1), InputBuffer::Insert::Stored);
        (void)buffer.take(tick);
    }
    EXPECT_EQ(buffer.insert(5, input(1), 80), InputBuffer::Insert::Late);
    EXPECT_EQ(buffer.insert(70, input(1), 80), InputBuffer::Insert::Duplicate);
}

TEST(InputBuffer, TooFarAheadIsRefused) {
    InputBuffer buffer(10);
    EXPECT_EQ(buffer.insert(10 + kMaxInputLeadTicks, input(1), 10), InputBuffer::Insert::Stored);
    EXPECT_EQ(buffer.insert(11 + kMaxInputLeadTicks, input(1), 10), InputBuffer::Insert::TooFarAhead);
    EXPECT_EQ(buffer.pending(), 1u);
}

} // namespace
} // namespace engine::net
