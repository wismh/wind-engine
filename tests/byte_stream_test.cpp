#include <gtest/gtest.h>

#include <engine/net/byte_stream.h>

namespace engine::net {

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
    EXPECT_EQ(reader.read_bool(), true);
    EXPECT_EQ(reader.read_bool(), false);
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

TEST(ByteStream, ReaderBoundsCheck) {
    std::vector<std::uint8_t> short_buf = {1, 2};
    ByteReader reader(short_buf);

    EXPECT_EQ(reader.read_u8(), 1);
    EXPECT_EQ(reader.read_u8(), 2);
    // Buffer exhausted
    EXPECT_EQ(reader.read_u32(), std::nullopt);
    EXPECT_FALSE(reader.is_ok());
}

} // namespace engine::net
