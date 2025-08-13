#include <gtest/gtest.h>

#include "render/framebuffer_image.h"

#include <cstdint>
#include <vector>

TEST(FramebufferImage, FlipsRowsAndMakesAnOpaqueWindowOpaque) {
    // Bottom row first, as glReadPixels returns it: row 0 is the bottom of a 2x2 window.
    const std::vector<std::uint8_t> bottom_up{
            10, 11, 12, 0,   20, 21, 22, 128,  // bottom
            30, 31, 32, 64,  40, 41, 42, 255,  // top
    };
    const engine::render::TextureDesc image = engine::render::framebuffer_image(bottom_up, 2, 2, false);
    ASSERT_EQ(image.width, 2);
    ASSERT_EQ(image.height, 2);
    const std::vector<std::uint8_t> expected{
            30, 31, 32, 255, 40, 41, 42, 255,
            10, 11, 12, 255, 20, 21, 22, 255,
    };
    EXPECT_EQ(image.rgba, expected);
}

TEST(FramebufferImage, TransparentWindowKeepsStraightAlpha) {
    // Premultiplied: half-transparent white is 128,128,128,128. Fully transparent stays black.
    const std::vector<std::uint8_t> bottom_up{128, 128, 128, 128, 0, 0, 0, 0, 40, 20, 10, 255};
    const engine::render::TextureDesc image = engine::render::framebuffer_image(bottom_up, 3, 1, true);
    const std::vector<std::uint8_t> expected{255, 255, 255, 128, 0, 0, 0, 0, 40, 20, 10, 255};
    EXPECT_EQ(image.rgba, expected);
}

TEST(FramebufferImage, WrongSizeIsEmpty) {
    const std::vector<std::uint8_t> bytes(12, 0);
    EXPECT_TRUE(engine::render::framebuffer_image(bytes, 2, 2, false).rgba.empty());
    EXPECT_TRUE(engine::render::framebuffer_image(bytes, 0, 3, false).rgba.empty());
    EXPECT_TRUE(engine::render::framebuffer_image({}, 0, 0, false).rgba.empty());
}
