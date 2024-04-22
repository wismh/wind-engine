#include <gtest/gtest.h>

#include <engine/igame.h>

#include <cstdint>
#include <vector>

#if defined(ENGINE_WITH_WINDOW)
#include "render/opengl/window_system.h"
#endif

namespace {

class DummyGame final : public engine::GameBase {};

TEST(WindowIcon, DefaultIsNullopt) {
    DummyGame game;
    EXPECT_FALSE(game.window_icon().has_value());
}

#if defined(ENGINE_WITH_WINDOW)

engine::render::TextureDesc make_desc(int width, int height, std::vector<std::uint8_t> rgba) {
    engine::render::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.rgba = std::move(rgba);
    return desc;
}

TEST(WindowIcon, SurfaceHasRequestedDimensions) {
    const auto desc = make_desc(2, 1, std::vector<std::uint8_t>(2 * 1 * 4, 0));
    SDL_Surface* surface = engine::make_icon_surface(desc);
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(surface->w, 2);
    EXPECT_EQ(surface->h, 1);
    SDL_DestroySurface(surface);
}

TEST(WindowIcon, SurfacePreservesPixelByteLayout) {
    // 1x2 RGBA image: opaque red pixel, then translucent green pixel.
    const std::vector<std::uint8_t> pixels = {
            255, 0, 0, 255,
            0, 255, 0, 128,
    };
    const auto desc = make_desc(1, 2, pixels);
    SDL_Surface* surface = engine::make_icon_surface(desc);
    ASSERT_NE(surface, nullptr);
    ASSERT_EQ(surface->pitch, 1 * 4);

    const auto* row0 = static_cast<const std::uint8_t*>(surface->pixels);
    const auto* row1 = row0 + surface->pitch;
    EXPECT_EQ(row0[0], 255);
    EXPECT_EQ(row0[1], 0);
    EXPECT_EQ(row0[2], 0);
    EXPECT_EQ(row0[3], 255);
    EXPECT_EQ(row1[0], 0);
    EXPECT_EQ(row1[1], 255);
    EXPECT_EQ(row1[2], 0);
    EXPECT_EQ(row1[3], 128);

    SDL_DestroySurface(surface);
}

TEST(WindowIcon, RejectsUndersizedBuffer) {
    const auto desc = make_desc(4, 4, std::vector<std::uint8_t>{1, 2, 3, 4});
    EXPECT_EQ(engine::make_icon_surface(desc), nullptr);
}

TEST(WindowIcon, RejectsNonPositiveDimensions) {
    const auto desc = make_desc(0, 4, std::vector<std::uint8_t>{});
    EXPECT_EQ(engine::make_icon_surface(desc), nullptr);
}

TEST(WindowIcon, SetIconIsNoopWithoutWindow) {
    engine::WindowSystem window;
    const auto desc = make_desc(1, 1, std::vector<std::uint8_t>(4, 0));
    // No SDL_Init(SDL_INIT_VIDEO), no window created: must not crash.
    window.set_icon(desc);
}

#endif

}
