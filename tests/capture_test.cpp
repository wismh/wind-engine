#include <gtest/gtest.h>

#include "capture/format_select.h"
#include "capture/frame_convert.h"

#include <engine/capture/capture_system.h>

#if defined(ENGINE_WITH_WINDOW)
#include <SDL3/SDL_surface.h>
#endif

#include <array>
#include <cstdint>
#include <vector>

namespace {

using engine::CaptureFormat;

constexpr std::array<CaptureFormat, 4> kOffered{{
        {640, 480, 30.f},
        {1280, 720, 30.f},
        {1280, 720, 60.f},
        {1920, 1080, 30.f},
}};

}

TEST(CaptureFormatSelect, EmptyOfferPicksNothing) {
    EXPECT_FALSE(engine::capture::pick_format({}, std::nullopt).has_value());
    EXPECT_FALSE(engine::capture::pick_format({}, CaptureFormat{640, 480, 30.f}).has_value());
}

TEST(CaptureFormatSelect, NoRequestPicksLargestThenFastest) {
    constexpr std::array<CaptureFormat, 3> offered{{{1280, 720, 30.f}, {1920, 1080, 15.f}, {1920, 1080, 30.f}}};
    EXPECT_EQ(engine::capture::pick_format(offered, std::nullopt), (CaptureFormat{1920, 1080, 30.f}));
}

TEST(CaptureFormatSelect, ExactRequestWins) {
    EXPECT_EQ(engine::capture::pick_format(kOffered, CaptureFormat{1280, 720, 60.f}), (CaptureFormat{1280, 720, 60.f}));
}

TEST(CaptureFormatSelect, ClosestPictureSizeWins) {
    // 1600x900 is 1.44M pixels: 1280x720 is 0.52M away, 1920x1080 is 0.63M away.
    EXPECT_EQ(
            engine::capture::pick_format(kOffered, CaptureFormat{1600, 900, 30.f}), (CaptureFormat{1280, 720, 30.f}));
}

TEST(CaptureFormatSelect, FrameRateBreaksASizeTie) {
    EXPECT_EQ(engine::capture::pick_format(kOffered, CaptureFormat{1280, 720, 50.f}), (CaptureFormat{1280, 720, 60.f}));
    EXPECT_EQ(engine::capture::pick_format(kOffered, CaptureFormat{1280, 720, 24.f}), (CaptureFormat{1280, 720, 30.f}));
}

TEST(CaptureFormatSelect, EarlierEntryWinsAFullTie) {
    constexpr std::array<CaptureFormat, 2> offered{{{640, 480, 30.f}, {640, 480, 30.f}}};
    EXPECT_EQ(engine::capture::pick_format(offered, CaptureFormat{640, 480, 30.f}), offered[0]);
}

TEST(CaptureSystem, NotInitializedIsEmptyAndRefusesOpen) {
    engine::CaptureSystem capture;
    EXPECT_FALSE(capture.is_supported());
    EXPECT_TRUE(capture.devices().empty());
    const auto session = capture.open(engine::CaptureDeviceId{1});
    ASSERT_FALSE(session.has_value());
    EXPECT_EQ(session.error(), engine::CaptureError::Unsupported);
    capture.close_all();
}

TEST(CaptureSystem, InitAndDisposeAreIdempotent) {
    engine::CaptureSystem capture;
    capture.dispose();
#if defined(ENGINE_WITH_WINDOW)
    // The SDL camera subsystem can fail to start on a machine without a camera driver.
    if (!capture.init()) {
        GTEST_SKIP() << "SDL camera subsystem is unavailable";
    }
    EXPECT_TRUE(capture.init());
#endif
    capture.dispose();
    capture.dispose();
    EXPECT_TRUE(capture.devices().empty());
}

#if defined(ENGINE_WITH_WINDOW)

TEST(CaptureSystem, UnknownDeviceIsNotOpened) {
    engine::CaptureSystem capture;
    if (!capture.init()) {
        GTEST_SKIP() << "SDL camera subsystem is unavailable";
    }
    const auto session = capture.open(engine::CaptureDeviceId{0xFFFFFFFFu});
    ASSERT_FALSE(session.has_value());
    EXPECT_TRUE(
            session.error() == engine::CaptureError::NoSuchDevice ||
            session.error() == engine::CaptureError::Unsupported);
}

TEST(CaptureFrameConvert, PackedRgbBecomesRgbaTopRowFirst) {
    SDL_Surface* surface = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_BGRA32);
    ASSERT_NE(surface, nullptr);
    auto* pixels = static_cast<std::uint8_t*>(surface->pixels);
    const auto put = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        std::uint8_t* p = pixels + y * surface->pitch + x * 4;
        p[0] = b;
        p[1] = g;
        p[2] = r;
        p[3] = 255;
    };
    put(0, 0, 10, 20, 30);
    put(1, 0, 40, 50, 60);
    put(0, 1, 70, 80, 90);
    put(1, 1, 100, 110, 120);

    std::vector<std::uint8_t> rgba;
    ASSERT_TRUE(engine::capture::convert_surface_to_rgba(surface, rgba));
    SDL_DestroySurface(surface);

    const std::vector<std::uint8_t> expected{10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 100, 110, 120, 255};
    EXPECT_EQ(rgba, expected);
}

TEST(CaptureFrameConvert, YuvBecomesOpaqueGray) {
    // YUY2: Y0 U Y1 V per two pixels. Mid-gray luma with neutral chroma stays gray after conversion.
    SDL_Surface* surface = SDL_CreateSurface(2, 1, SDL_PIXELFORMAT_YUY2);
    ASSERT_NE(surface, nullptr);
    auto* pixels = static_cast<std::uint8_t*>(surface->pixels);
    pixels[0] = 126;
    pixels[1] = 128;
    pixels[2] = 126;
    pixels[3] = 128;

    std::vector<std::uint8_t> rgba;
    ASSERT_TRUE(engine::capture::convert_surface_to_rgba(surface, rgba));
    SDL_DestroySurface(surface);

    ASSERT_EQ(rgba.size(), 8u);
    for (int pixel = 0; pixel < 2; ++pixel) {
        const std::uint8_t* p = rgba.data() + pixel * 4;
        EXPECT_NEAR(p[0], p[1], 3);
        EXPECT_NEAR(p[1], p[2], 3);
        EXPECT_EQ(p[3], 255);
    }
}

TEST(CaptureFrameConvert, NullSurfaceFails) {
    std::vector<std::uint8_t> rgba;
    EXPECT_FALSE(engine::capture::convert_surface_to_rgba(nullptr, rgba));
}

#endif
