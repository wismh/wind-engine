#include <gtest/gtest.h>

#if defined(ENGINE_WITH_WINDOW)
#include "render/opengl/opengl_texture.h"

#include <cstdint>
#include <vector>

namespace {

TEST(OpenGLTexture, FlipImageVertically1x2SwapsRows) {
    // 1x2 image: row 0 is Red, row 1 is Green
    const std::vector<std::uint8_t> input = {
            255, 0, 0, 255,   // row 0: Red
            0, 255, 0, 255,   // row 1: Green
    };

    const auto flipped = engine::render::flip_image_vertically(input, 1, 2);
    ASSERT_EQ(flipped.size(), 8u);

    // Row 0 of flipped must be Green
    EXPECT_EQ(flipped[0], 0);
    EXPECT_EQ(flipped[1], 255);
    EXPECT_EQ(flipped[2], 0);
    EXPECT_EQ(flipped[3], 255);

    // Row 1 of flipped must be Red
    EXPECT_EQ(flipped[4], 255);
    EXPECT_EQ(flipped[5], 0);
    EXPECT_EQ(flipped[6], 0);
    EXPECT_EQ(flipped[7], 255);
}

TEST(OpenGLTexture, FlipImageVertically2x2PreservesHorizontalOrdering) {
    // 2x2 image:
    // Row 0: Pixel A, Pixel B
    // Row 1: Pixel C, Pixel D
    const std::vector<std::uint8_t> input = {
            1, 2, 3, 4,       5, 6, 7, 8,         // row 0
            9, 10, 11, 12,    13, 14, 15, 16,     // row 1
    };

    const auto flipped = engine::render::flip_image_vertically(input, 2, 2);
    ASSERT_EQ(flipped.size(), 16u);

    // Row 0 of flipped must be Pixel C, Pixel D
    EXPECT_EQ(flipped[0], 9);
    EXPECT_EQ(flipped[1], 10);
    EXPECT_EQ(flipped[2], 11);
    EXPECT_EQ(flipped[3], 12);
    EXPECT_EQ(flipped[4], 13);
    EXPECT_EQ(flipped[5], 14);
    EXPECT_EQ(flipped[6], 15);
    EXPECT_EQ(flipped[7], 16);

    // Row 1 of flipped must be Pixel A, Pixel B
    EXPECT_EQ(flipped[8], 1);
    EXPECT_EQ(flipped[9], 2);
    EXPECT_EQ(flipped[10], 3);
    EXPECT_EQ(flipped[11], 4);
    EXPECT_EQ(flipped[12], 5);
    EXPECT_EQ(flipped[13], 6);
    EXPECT_EQ(flipped[14], 7);
    EXPECT_EQ(flipped[15], 8);
}

TEST(OpenGLTexture, FlipImageVerticallySingleRowUnchanged) {
    const std::vector<std::uint8_t> input = {
            10, 20, 30, 40,
            50, 60, 70, 80,
    };
    const auto flipped = engine::render::flip_image_vertically(input, 2, 1);
    ASSERT_EQ(flipped, input);
}

TEST(OpenGLTexture, FlipImageVerticallyInvalidDimensionsOrEmpty) {
    const std::vector<std::uint8_t> input = {255, 0, 0, 255};

    EXPECT_TRUE(engine::render::flip_image_vertically({}, 1, 1).empty());
    EXPECT_TRUE(engine::render::flip_image_vertically(input, 0, 1).empty());
    EXPECT_TRUE(engine::render::flip_image_vertically(input, 1, 0).empty());
    EXPECT_TRUE(engine::render::flip_image_vertically(input, -1, 1).empty());
    EXPECT_TRUE(engine::render::flip_image_vertically(input, 1, -1).empty());
    // Input smaller than width * height * 4
    EXPECT_TRUE(engine::render::flip_image_vertically(input, 2, 2).empty());
}

}

#endif
