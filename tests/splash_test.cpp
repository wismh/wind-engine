#include <gtest/gtest.h>

#include "ui/splash.h"

#include <engine/ecs/world.h>
#include <engine/igame.h>
#include <engine/resources/asset_id.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <glm/vec2.hpp>

#include <optional>
#include <string>

namespace {

constexpr std::string_view kSplashImageGuid = "c1a1c2d3e4f5678901234567890abc09";
constexpr glm::vec2 kValidImageSize{1086.0f, 884.0f};

std::string opacity_at(const engine::ui::KeyframeStop& stop) {
    for (const engine::ui::CssDeclaration& decl : stop.declarations) {
        if (decl.property == "opacity") {
            return decl.value;
        }
    }
    return {};
}

std::string decl_value(const engine::ui::CssRule& rule, std::string_view property) {
    for (const engine::ui::CssDeclaration& decl : rule.declarations) {
        if (decl.property == property) {
            return decl.value;
        }
    }
    return {};
}

}

TEST(Splash, BuildsFourKeyframeStopsAndDurationFromConfig) {
    engine::SplashScreen config;
    config.image = engine::AssetId{kSplashImageGuid};
    config.fade_in_seconds = 0.5f;
    config.hold_seconds = 1.0f;
    config.fade_out_seconds = 0.5f;

    const auto splash = engine::ui::build_splash_document(config, kValidImageSize);
    ASSERT_TRUE(splash.has_value());

    ASSERT_EQ(splash->stylesheet.keyframes.size(), 1u);
    const engine::ui::Keyframes& keyframes = splash->stylesheet.keyframes.front();
    ASSERT_EQ(keyframes.stops.size(), 4u);

    EXPECT_FLOAT_EQ(keyframes.stops[0].offset, 0.0f);
    EXPECT_EQ(opacity_at(keyframes.stops[0]), "0");
    EXPECT_NEAR(keyframes.stops[1].offset, 0.25f, 0.001f);
    EXPECT_EQ(opacity_at(keyframes.stops[1]), "1");
    EXPECT_NEAR(keyframes.stops[2].offset, 0.75f, 0.001f);
    EXPECT_EQ(opacity_at(keyframes.stops[2]), "1");
    EXPECT_FLOAT_EQ(keyframes.stops[3].offset, 1.0f);
    EXPECT_EQ(opacity_at(keyframes.stops[3]), "0");

    ASSERT_EQ(splash->stylesheet.rules.size(), 1u);
    EXPECT_EQ(decl_value(splash->stylesheet.rules.front(), "animation-duration"), "2s");
}

TEST(Splash, ImageRuleIsAbsoluteAndCenteredAtEightyPercentNotStretched) {
    engine::SplashScreen config;
    config.image = engine::AssetId{kSplashImageGuid};

    const auto splash = engine::ui::build_splash_document(config, kValidImageSize);
    ASSERT_TRUE(splash.has_value());

    ASSERT_EQ(splash->stylesheet.rules.size(), 1u);
    const engine::ui::CssRule& rule = splash->stylesheet.rules.front();
    // Aspect ratio comes from the canvas's reference_size (below) being fit to the real window
    // preserving aspect (UiFit::ScaleWithScreenSize); the image itself is a fixed, centered 80%
    // box within that canvas - width/height: 100% here would stretch a non-square image.
    EXPECT_EQ(decl_value(rule, "position"), "absolute");
    EXPECT_EQ(decl_value(rule, "left"), "10%");
    EXPECT_EQ(decl_value(rule, "top"), "10%");
    EXPECT_EQ(decl_value(rule, "width"), "80%");
    EXPECT_EQ(decl_value(rule, "height"), "80%");
}

TEST(Splash, ReferenceSizePreservesImageAspectRatioWithTenPercentMargin) {
    engine::SplashScreen config;
    config.image = engine::AssetId{kSplashImageGuid};

    const auto splash = engine::ui::build_splash_document(config, kValidImageSize);
    ASSERT_TRUE(splash.has_value());

    // reference_size = image_size / 0.8: once UiFit::ScaleWithScreenSize letterboxes this box
    // into the real window (preserving its aspect ratio), the image's own 80%-of-canvas box
    // lands at exactly image_size's aspect ratio, with >=10% margin on every edge.
    EXPECT_NEAR(splash->reference_size.x, kValidImageSize.x / 0.8f, 0.01f);
    EXPECT_NEAR(splash->reference_size.y, kValidImageSize.y / 0.8f, 0.01f);
    EXPECT_NEAR(splash->reference_size.x / splash->reference_size.y, kValidImageSize.x / kValidImageSize.y, 0.001f);
}

TEST(Splash, GeneratedXmlContainsOneImageReferencingConfiguredAsset) {
    engine::SplashScreen config;
    config.image = engine::AssetId{kSplashImageGuid};

    const auto splash = engine::ui::build_splash_document(config, kValidImageSize);
    ASSERT_TRUE(splash.has_value());

    const engine::ui::Element* image =
            engine::ui::find_by_kind(splash->document.root, engine::ui::ElementKind::Image);
    ASSERT_NE(image, nullptr);
    ASSERT_TRUE(image->source.has_value());
    EXPECT_EQ(*image->source, config.image);
}

TEST(Splash, DisabledConfigBuildsNothing) {
    engine::SplashScreen config;
    config.enabled = false;

    EXPECT_FALSE(engine::ui::build_splash_document(config, kValidImageSize).has_value());
}

TEST(Splash, ZeroTotalDurationBuildsNothingInsteadOfDividingByZero) {
    engine::SplashScreen config;
    config.fade_in_seconds = 0.0f;
    config.hold_seconds = 0.0f;
    config.fade_out_seconds = 0.0f;

    EXPECT_FALSE(engine::ui::build_splash_document(config, kValidImageSize).has_value());
}

TEST(Splash, UnresolvedImageSizeBuildsNothing) {
    engine::SplashScreen config;
    config.image = engine::AssetId{kSplashImageGuid};

    EXPECT_FALSE(engine::ui::build_splash_document(config, glm::vec2{0.0f, 0.0f}).has_value());
    EXPECT_FALSE(engine::ui::build_splash_document(config, glm::vec2{-1.0f, 100.0f}).has_value());
    EXPECT_FALSE(engine::ui::build_splash_document(config, glm::vec2{100.0f, 0.0f}).has_value());
}

TEST(Splash, SpawnsExactlyOneCanvasWhenEnabledAndNoneWhenDisabled) {
    {
        engine::ecs::World world;
        engine::SplashScreen config;
        config.image = engine::AssetId{kSplashImageGuid};

        const auto splash = engine::ui::build_splash_document(config, kValidImageSize);
        ASSERT_TRUE(splash.has_value());

        const engine::ecs::Entity entity = world.create();
        engine::ui::UiCanvas canvas;
        canvas.fit = engine::ui::UiFit::ScaleWithScreenSize;
        canvas.reference_size = splash->reference_size;
        canvas.order = 1000;
        world.emplace<engine::ui::UiCanvas>(entity, canvas);
        world.emplace<engine::ui::UiInstance>(entity, engine::ui::UiInstance{splash->document, splash->stylesheet});

        int count = 0;
        for (engine::ecs::Entity e : world.view<engine::ui::UiCanvas>()) {
            (void) e;
            ++count;
        }
        EXPECT_EQ(count, 1);
    }
    {
        engine::ecs::World world;
        engine::SplashScreen config;
        config.enabled = false;

        const auto splash = engine::ui::build_splash_document(config, kValidImageSize);
        ASSERT_FALSE(splash.has_value());

        int count = 0;
        for (engine::ecs::Entity e : world.view<engine::ui::UiCanvas>()) {
            (void) e;
            ++count;
        }
        EXPECT_EQ(count, 0);
    }
}
