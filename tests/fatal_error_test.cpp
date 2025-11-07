#include <gtest/gtest.h>

#include <engine/core/bound_windows.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/systems.h>
#include <engine/ecs/transform.h>
#include <engine/ecs/world.h>
#include <engine/render/command_buffer.h>
#include <engine/render/graphics.h>
#include <engine/render/sprite.h>
#include <engine/resources/asset_id.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/default_fatal_error.h>
#include <engine/ui/presentation.h>

#include <string>

#include "fixtures/fatal_fakes.h"

namespace {

constexpr engine::AssetId kMissing{"c1d2e3f4567890123456789012345abc"};

}

TEST(FatalError, GetOnAMissingAssetReportsOnce) {
    fakes::RecordingFatal fatal;
    engine::AssetsDb db(fatal);

    EXPECT_THROW((void) db.get<engine::render::ITexture>(kMissing), fakes::FatalReport);

    EXPECT_EQ(fatal.call_count, 1);
    EXPECT_NE(fatal.last_message.find(std::string(kMissing.hex())), std::string::npos);
}

TEST(FatalError, TryGetOnAMissingAssetDoesNotReport) {
    fakes::RecordingFatal fatal;
    engine::AssetsDb db(fatal);

    const auto result = db.try_get<engine::render::ITexture>(kMissing);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), engine::AssetError::NotFound);
    EXPECT_EQ(fatal.call_count, 0);
}

TEST(FatalError, DefaultFatalErrorWritesToStderrAndAborts) {
    EXPECT_DEATH(engine::default_fatal_error().report("the default exit"), "the default exit");
}

TEST(FatalError, SystemsWithoutAFatalUseTheDefaultExit) {
    engine::ecs::World world;
    engine::render::CommandBuffer commands;
    engine::register_engine_systems(world, engine::EngineSystemDeps{.commands = &commands});
    const engine::ecs::Entity camera = world.create();
    world.emplace<engine::Transform>(camera, engine::Transform{});
    world.emplace<engine::Camera>(camera, engine::Camera{});
    world.ctx<engine::ActiveCamera>().entity = camera;
    engine::ui::presentation_of(world).sizes.sizes[engine::kPrimaryWindow] = engine::ui::WindowSize{800, 600};
    world.ctx<engine::BoundWindows>().ids = {engine::kPrimaryWindow};
    const engine::ecs::Entity sprite = world.create();
    world.emplace<engine::Transform>(sprite, engine::Transform{});
    world.emplace<engine::render::Sprite>(sprite, engine::render::Sprite{});

    EXPECT_DEATH(world.run(engine::ecs::Schedule::Frame), "Sprite is missing mesh or material");
}
