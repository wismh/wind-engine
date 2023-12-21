#include <gtest/gtest.h>

#include <engine/net/byte_stream.h>

namespace engine::net {

enum class TestAction : std::uint8_t {
    Idle = 0,
    Move = 1,
    Attack = 2,
    Special = 42,
};

TEST(ByteStream, PrimitiveRoundTrip) {
    ByteWriter writer;
    writer.write_u8(42);
    writer.write_i8(-17);
    writer.write_u16(1337);
    writer.write_i16(-500);
    writer.write_u32(100000);
    writer.write_i32(-200000);
    writer.write_u64(0xDEADBEEFCAFEBABEULL);
    writer.write_i64(-9876543210LL);
    writer.write_float(3.14159f);
    writer.write_double(2.718281828459);
    writer.write_bool(true);
    writer.write_bool(false);

    ByteReader reader(writer.bytes());
    EXPECT_EQ(reader.read_u8(), 42);
    EXPECT_EQ(reader.read_i8(), -17);
    EXPECT_EQ(reader.read_u16(), 1337);
    EXPECT_EQ(reader.read_i16(), -500);
    EXPECT_EQ(reader.read_u32(), 100000);
    EXPECT_EQ(reader.read_i32(), -200000);
    EXPECT_EQ(reader.read_u64(), 0xDEADBEEFCAFEBABEULL);
    EXPECT_EQ(reader.read_i64(), -9876543210LL);
    EXPECT_FLOAT_EQ(*reader.read_float(), 3.14159f);
    EXPECT_DOUBLE_EQ(*reader.read_double(), 2.718281828459);
    EXPECT_EQ(reader.read_bool(), true);
    EXPECT_EQ(reader.read_bool(), false);
    EXPECT_TRUE(reader.is_ok());
    EXPECT_EQ(reader.remaining(), 0);
}

TEST(ByteStream, MathAndNetIdRoundTrip) {
    ByteWriter writer;
    const glm::vec2 v2{1.5f, -2.5f};
    const glm::vec3 v3{10.0f, 20.0f, 30.0f};
    const glm::vec4 v4{1.0f, 2.0f, 3.0f, 4.0f};
    const glm::quat q{0.7071f, 0.0f, 0.7071f, 0.0f};
    const NetId id{12345};

    writer.write_vec2(v2);
    writer.write_vec3(v3);
    writer.write_vec4(v4);
    writer.write_quat(q);
    writer.write_net_id(id);

    ByteReader reader(writer.span());
    const auto r_v2 = reader.read_vec2();
    ASSERT_TRUE(r_v2.has_value());
    EXPECT_EQ(*r_v2, v2);

    const auto r_v3 = reader.read_vec3();
    ASSERT_TRUE(r_v3.has_value());
    EXPECT_EQ(*r_v3, v3);

    const auto r_v4 = reader.read_vec4();
    ASSERT_TRUE(r_v4.has_value());
    EXPECT_EQ(*r_v4, v4);

    const auto r_q = reader.read_quat();
    ASSERT_TRUE(r_q.has_value());
    EXPECT_EQ(*r_q, q);

    const auto r_id = reader.read_net_id();
    ASSERT_TRUE(r_id.has_value());
    EXPECT_EQ(*r_id, id);

    EXPECT_TRUE(reader.is_ok());
    EXPECT_EQ(reader.remaining(), 0);
}

TEST(ByteStream, EnumAndGenericReadWrite) {
    ByteWriter writer;
    writer.write_enum(TestAction::Special);
    writer.write<float>(123.456f);
    writer.write<std::uint32_t>(777);
    writer.write<NetId>(NetId{888});

    ByteReader reader(writer.bytes());

    EXPECT_EQ(reader.read_enum<TestAction>(), TestAction::Special);

    float f = 0.0f;
    EXPECT_TRUE(reader.read(f));
    EXPECT_FLOAT_EQ(f, 123.456f);

    EXPECT_EQ(reader.read<std::uint32_t>(), 777u);
    EXPECT_EQ(reader.read<NetId>(), NetId{888});
    EXPECT_TRUE(reader.is_ok());
    EXPECT_EQ(reader.remaining(), 0);
}

TEST(ByteStream, MathAndStringRoundTrip) {
    ByteWriter writer;
    writer.write_vec2(glm::vec2(1.5f, -2.5f));
    writer.write_vec3(glm::vec3(10.0f, 20.0f, 30.0f));
    writer.write_string("Hello Wind Multiplayer!");

    ByteReader reader(writer.bytes());
    const auto v2 = reader.read_vec2();
    ASSERT_TRUE(v2.has_value());
    EXPECT_FLOAT_EQ(v2->x, 1.5f);
    EXPECT_FLOAT_EQ(v2->y, -2.5f);

    const auto v3 = reader.read_vec3();
    ASSERT_TRUE(v3.has_value());
    EXPECT_FLOAT_EQ(v3->x, 10.0f);
    EXPECT_FLOAT_EQ(v3->y, 20.0f);
    EXPECT_FLOAT_EQ(v3->z, 30.0f);

    const auto str = reader.read_string();
    ASSERT_TRUE(str.has_value());
    EXPECT_EQ(*str, "Hello Wind Multiplayer!");
    EXPECT_TRUE(reader.is_ok());
    EXPECT_EQ(reader.remaining(), 0);
}

TEST(ByteStream, ReaderBoundsCheckAndLatch) {
    std::vector<std::uint8_t> short_buf = {1, 2};
    ByteReader reader(short_buf);

    EXPECT_EQ(reader.read_u8(), 1);
    EXPECT_EQ(reader.read_u8(), 2);
    // Buffer exhausted
    EXPECT_EQ(reader.read_u32(), std::nullopt);
    EXPECT_FALSE(reader.is_ok());
    EXPECT_FALSE(static_cast<bool>(reader));

    // Latch: further reads fail
    EXPECT_EQ(reader.read_u8(), std::nullopt);
}

TEST(ByteStream, SkipAndReset) {
    ByteWriter writer;
    writer.write_u32(111);
    writer.write_u32(222);
    writer.write_u32(333);

    ByteReader reader(writer.span());
    EXPECT_EQ(reader.read_u32(), 111u);

    // Skip the second u32 (4 bytes)
    EXPECT_TRUE(reader.skip(4));
    EXPECT_EQ(reader.read_u32(), 333u);

    // Skip past end fails
    EXPECT_FALSE(reader.skip(1));
    EXPECT_FALSE(reader.is_ok());

    // Reset recovers reader
    reader.reset(writer.span());
    EXPECT_TRUE(reader.is_ok());
    EXPECT_EQ(reader.read_u32(), 111u);
}

} // namespace engine::net
