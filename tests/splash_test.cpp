#include <gtest/gtest.h>

#include "ui/splash.h"

#include <engine/ecs/world.h>
#include <engine/igame.h>
#include <engine/resources/asset_id.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <optional>
#include <string>

namespace {

constexpr std::string_view kSplashImageGuid = "c1a1c2d3e4f5678901234567890abc09";

std::string opacity_at(const engine::ui::KeyframeStop& stop) {
    for (const engine::ui::CssDeclaration& decl : stop.declarations) {
        if (decl.property == "opacity") {
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

    const auto splash = engine::ui::build_splash_document(config);
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
    bool found_duration = false;
    for (const engine::ui::CssDeclaration& decl : splash->stylesheet.rules.front().declarations) {
        if (decl.property == "animation-duration") {
            EXPECT_EQ(decl.value, "2s");
            found_duration = true;
        }
    }
    EXPECT_TRUE(found_duration);
}

TEST(Splash, GeneratedXmlContainsOneImageReferencingConfiguredAsset) {
    engine::SplashScreen config;
    config.image = engine::AssetId{kSplashImageGuid};

    const auto splash = engine::ui::build_splash_document(config);
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

    EXPECT_FALSE(engine::ui::build_splash_document(config).has_value());
}

TEST(Splash, ZeroTotalDurationBuildsNothingInsteadOfDividingByZero) {
    engine::SplashScreen config;
    config.fade_in_seconds = 0.0f;
    config.hold_seconds = 0.0f;
    config.fade_out_seconds = 0.0f;

    EXPECT_FALSE(engine::ui::build_splash_document(config).has_value());
}

TEST(Splash, SpawnsExactlyOneCanvasWhenEnabledAndNoneWhenDisabled) {
    {
        engine::ecs::World world;
        engine::SplashScreen config;
        config.image = engine::AssetId{kSplashImageGuid};

        const auto splash = engine::ui::build_splash_document(config);
        ASSERT_TRUE(splash.has_value());

        const engine::ecs::Entity entity = world.create();
        engine::ui::UiCanvas canvas;
        canvas.fit = engine::ui::UiFit::FillWindow;
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

        const auto splash = engine::ui::build_splash_document(config);
        ASSERT_FALSE(splash.has_value());

        int count = 0;
        for (engine::ecs::Entity e : world.view<engine::ui::UiCanvas>()) {
            (void) e;
            ++count;
        }
        EXPECT_EQ(count, 0);
    }
}
