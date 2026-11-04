#include <gtest/gtest.h>

#include "render/texture_check.h"

#include <cstdint>
#include <vector>

namespace {

std::vector<std::uint8_t> image_with_marker(int stride, int height) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(stride) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < stride; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
                                          static_cast<std::size_t>(x)) *
                                  4u;
            rgba[i] = static_cast<std::uint8_t>(x);
            rgba[i + 1] = static_cast<std::uint8_t>(y);
            rgba[i + 2] = 3;
            rgba[i + 3] = 255;
        }
    }
    return rgba;
}

std::size_t flipped_index(int width, int height, int x, int y) {
    return (static_cast<std::size_t>(height - 1 - y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) *
            4u;
}

}

TEST(TextureRects, EmptyListIsValid) {
    EXPECT_TRUE(engine::render::texture_rects_error(0, 0, {}, {}, 0).empty());
    std::vector<std::uint8_t> dest{9};
    EXPECT_TRUE(engine::render::write_texture_rects(0, 0, {}, {}, 0, dest));
    EXPECT_EQ(dest, std::vector<std::uint8_t>{9});
}

TEST(TextureRects, RejectsARectThatCannotBeWritten) {
    const std::vector<std::uint8_t> rgba(4 * 3 * 4, 1);
    const engine::render::TextureRect pixel[] = {{0, 0, 1, 1}};
    const engine::render::TextureRect empty[] = {{0, 0, 0, 1}};
    const engine::render::TextureRect negative[] = {{-1, 0, 1, 1}};
    const engine::render::TextureRect past_right[] = {{2, 0, 3, 1}};
    const engine::render::TextureRect past_bottom[] = {{0, 2, 1, 2}};
    const engine::render::TextureRect stride[] = {{0, 0, 2, 1}};
    const engine::render::TextureRect last_row[] = {{0, 2, 4, 1}};
    const engine::render::TextureRect one_bad[] = {{0, 0, 1, 1}, {9, 0, 1, 1}};
    EXPECT_FALSE(engine::render::texture_rects_error(0, 3, pixel, rgba, 4).empty());
    EXPECT_FALSE(engine::render::texture_rects_error(4, 3, empty, rgba, 4).empty());
    EXPECT_FALSE(engine::render::texture_rects_error(4, 3, pixel, rgba, 0).empty());
    EXPECT_FALSE(engine::render::texture_rects_error(4, 3, negative, rgba, 4).empty());
    EXPECT_FALSE(engine::render::texture_rects_error(4, 3, past_right, rgba, 4).empty());
    EXPECT_FALSE(engine::render::texture_rects_error(4, 3, past_bottom, rgba, 4).empty());
    EXPECT_FALSE(engine::render::texture_rects_error(4, 3, stride, rgba, 1).empty());

    std::vector<std::uint8_t> short_rgba(4 * 3 * 4 - 1, 1);
    EXPECT_FALSE(engine::render::texture_rects_error(4, 3, last_row, short_rgba, 4).empty());

    std::vector<std::uint8_t> dest(4 * 3 * 4, 7);
    const std::vector<std::uint8_t> before = dest;
    EXPECT_FALSE(engine::render::write_texture_rects(4, 3, one_bad, rgba, 4, dest));
    EXPECT_EQ(dest, before);
    EXPECT_FALSE(engine::render::write_texture_rects(4, 3, pixel, rgba, 4, std::span<std::uint8_t>(dest.data(), 4)));
    EXPECT_EQ(dest, before);
}

TEST(TextureRects, AcceptsABufferThatEndsOnTheRect) {
    // Stride 6, but the one rect ends on column 3 of the last row: two full rows plus three pixels.
    constexpr int kStride = 6;
    const std::size_t bytes = static_cast<std::size_t>(2 * kStride + 3) * 4u;
    const std::vector<std::uint8_t> rgba(bytes, 1);
    const engine::render::TextureRect rects[] = {{.x = 1, .y = 2, .width = 2, .height = 1}};
    EXPECT_TRUE(engine::render::texture_rects_error(4, 3, rects, rgba, kStride).empty());
    EXPECT_FALSE(engine::render::texture_rects_error(
            4, 3, rects, std::span<const std::uint8_t>(rgba.data(), bytes - 1), kStride)
                         .empty());
}

TEST(TextureRects, GpuRowIsTheBottomOfTheRect) {
    const engine::render::TextureRect top{.x = 1, .y = 0, .width = 2, .height = 2};
    EXPECT_EQ(engine::render::texture_rect_gpu_y(3, top), 1);
    const engine::render::TextureRect bottom{.x = 0, .y = 2, .width = 4, .height = 1};
    EXPECT_EQ(engine::render::texture_rect_gpu_y(3, bottom), 0);
}

TEST(TextureRects, WritesOnlyTheRectAndPacksItsBottomRowFirst) {
    constexpr int kWidth = 4;
    constexpr int kHeight = 3;
    constexpr int kStride = 6;
    const std::vector<std::uint8_t> rgba = image_with_marker(kStride, kHeight);
    const engine::render::TextureRect rects[] = {
            {.x = 1, .y = 0, .width = 2, .height = 2},
            {.x = 0, .y = 2, .width = 1, .height = 1},
    };

    std::vector<std::uint8_t> dest(kWidth * kHeight * 4, 0);
    ASSERT_TRUE(engine::render::write_texture_rects(kWidth, kHeight, rects, rgba, kStride, dest));

    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const bool written = (x >= 1 && x < 3 && y < 2) || (x == 0 && y == 2);
            const std::size_t dst = flipped_index(kWidth, kHeight, x, y);
            const std::size_t src = (static_cast<std::size_t>(y) * kStride + static_cast<std::size_t>(x)) * 4u;
            if (written) {
                EXPECT_EQ(dest[dst], rgba[src]) << x << "," << y;
                EXPECT_EQ(dest[dst + 1], rgba[src + 1]) << x << "," << y;
                EXPECT_EQ(dest[dst + 3], 255) << x << "," << y;
            } else {
                EXPECT_EQ(dest[dst], 0) << x << "," << y;
                EXPECT_EQ(dest[dst + 3], 0) << x << "," << y;
            }
        }
    }

    std::vector<std::uint8_t> packed(2 * 2 * 4);
    ASSERT_TRUE(engine::render::pack_texture_rect(rects[0], rgba, kStride, packed));
    const int gpu_y = engine::render::texture_rect_gpu_y(kHeight, rects[0]);
    for (int row = 0; row < rects[0].height; ++row) {
        const std::size_t packed_row = static_cast<std::size_t>(row) * static_cast<std::size_t>(rects[0].width) * 4u;
        const std::size_t image_row =
                (static_cast<std::size_t>(gpu_y + row) * kWidth + static_cast<std::size_t>(rects[0].x)) * 4u;
        EXPECT_EQ(std::vector<std::uint8_t>(packed.begin() + static_cast<std::ptrdiff_t>(packed_row),
                          packed.begin() + static_cast<std::ptrdiff_t>(packed_row + 8)),
                std::vector<std::uint8_t>(dest.begin() + static_cast<std::ptrdiff_t>(image_row),
                        dest.begin() + static_cast<std::ptrdiff_t>(image_row + 8)));
    }
}

TEST(TextureRects, PackRefusesARectItCannotHold) {
    const std::vector<std::uint8_t> rgba(16, 1);
    std::vector<std::uint8_t> dest{1, 2, 3, 4};
    EXPECT_FALSE(engine::render::pack_texture_rect({0, 0, 0, 1}, rgba, 2, dest));
    EXPECT_FALSE(engine::render::pack_texture_rect({0, 0, 2, 2}, rgba, 2, dest));
    EXPECT_EQ(dest, (std::vector<std::uint8_t>{1, 2, 3, 4}));
}
