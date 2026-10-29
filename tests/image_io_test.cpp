#include <gtest/gtest.h>

#include <engine/resources/assets_db.h>
#include <engine/resources/image_io.h>

#include "fixtures/fatal_fakes.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

using engine::render::TextureDesc;

TextureDesc solid(int width, int height, std::uint8_t red) {
    TextureDesc image;
    image.width = width;
    image.height = height;
    image.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 255u);
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        image.rgba[i] = red;
    }
    return image;
}

TEST(ImageIo, PngRoundTrips) {
    const TextureDesc source = solid(5, 3, 40);
    const std::vector<std::uint8_t> bytes = engine::encode_png_rgba(source);
    ASSERT_FALSE(bytes.empty());
    const auto decoded =
            engine::decode_png_rgba(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->width, 5);
    EXPECT_EQ(decoded->height, 3);
    EXPECT_EQ(decoded->rgba, source.rgba);
}

TEST(ImageIo, FitWithinKeepsAspectOfAWideImage) {
    const TextureDesc fitted = engine::fit_within(solid(400, 100, 200), 128);
    EXPECT_EQ(fitted.width, 128);
    EXPECT_EQ(fitted.height, 32);
    EXPECT_EQ(fitted.rgba.size(), 128u * 32u * 4u);
}

TEST(ImageIo, FitWithinKeepsAspectOfATallImage) {
    const TextureDesc fitted = engine::fit_within(solid(100, 400, 200), 128);
    EXPECT_EQ(fitted.width, 32);
    EXPECT_EQ(fitted.height, 128);
}

TEST(ImageIo, FitWithinNeverMakesAnEdgeZero) {
    const TextureDesc fitted = engine::fit_within(solid(1000, 2, 200), 16);
    EXPECT_EQ(fitted.width, 16);
    EXPECT_EQ(fitted.height, 1);
}

TEST(ImageIo, FitWithinLeavesASmallImageAlone) {
    const TextureDesc image = solid(20, 10, 7);
    const TextureDesc fitted = engine::fit_within(image, 128);
    EXPECT_EQ(fitted.width, 20);
    EXPECT_EQ(fitted.height, 10);
    EXPECT_EQ(fitted.rgba, image.rgba);
}

TEST(ImageIo, FitWithinRejectsShortPixelsAndBadEdges) {
    TextureDesc short_pixels = solid(8, 8, 1);
    short_pixels.rgba.pop_back();
    EXPECT_EQ(engine::fit_within(short_pixels, 4).width, 0);
    EXPECT_EQ(engine::fit_within(solid(8, 8, 1), 0).width, 0);
}

TEST(AssetsDbRuntimeTexture, IsFoundByIdAndReplaced) {
    fakes::QuietFatal fatal;
    engine::AssetsDb db(fatal);
    const engine::AssetId id{"0123456789abcdef0123456789abcdef"};
    EXPECT_EQ(db.runtime_texture(id), nullptr);
    db.set_runtime_texture(id, solid(2, 2, 9));
    ASSERT_NE(db.runtime_texture(id), nullptr);
    EXPECT_EQ(db.runtime_texture(id)->width, 2);
    db.set_runtime_texture(id, solid(3, 3, 9));
    EXPECT_EQ(db.runtime_texture(id)->width, 3);
    db.clear_runtime_textures();
    EXPECT_EQ(db.runtime_texture(id), nullptr);
}

}
