#include <gtest/gtest.h>

#include "core/base64.h"

#include <cstdint>
#include <string>
#include <vector>

TEST(Base64, KnownVectors) {
    const std::string text = "foobar";
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    const std::string expected[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    for (std::size_t n = 0; n <= bytes.size(); ++n) {
        const std::span<const std::uint8_t> prefix(bytes.data(), n);
        EXPECT_EQ(engine::base64_encode(prefix), expected[n]);
        const auto decoded = engine::base64_decode(expected[n]);
        ASSERT_TRUE(decoded.has_value()) << expected[n];
        EXPECT_EQ(*decoded, std::vector<std::uint8_t>(prefix.begin(), prefix.end()));
    }
}

TEST(Base64, RoundTripsEveryByte) {
    std::vector<std::uint8_t> bytes;
    for (int i = 0; i < 256; ++i) {
        bytes.push_back(static_cast<std::uint8_t>(i));
    }
    EXPECT_EQ(engine::base64_decode(engine::base64_encode(bytes)), bytes);
}

TEST(Base64, RejectsMalformedText) {
    EXPECT_FALSE(engine::base64_decode("Zm9").has_value());
    EXPECT_FALSE(engine::base64_decode("Zm9v!A==").has_value());
    EXPECT_FALSE(engine::base64_decode("Zg==Zm9v").has_value());
    EXPECT_FALSE(engine::base64_decode("Z===").has_value());
    EXPECT_FALSE(engine::base64_decode("Zm=v").has_value());
}
