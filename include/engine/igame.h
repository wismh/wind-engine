#pragma once

#include <engine/builtin_ids.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/world.h>
#include <engine/resources/asset_id.h>

#include <glm/vec2.hpp>

#include <optional>
#include <string>

namespace engine {

// Three phases instead of one duration_seconds: fade-in and fade-out need to be timed
// independently from the hold in the middle (SDD §20.1).
struct SplashScreen {
    bool enabled = true;
    AssetId image = builtin::splash_wind;
    float fade_in_seconds = 0.4f;
    float hold_seconds = 1.0f;
    float fade_out_seconds = 0.4f;
};

class IGame {
public:
    virtual ~IGame() = default;

    virtual std::string window_title() const {
        return "Game";
    }

    virtual glm::ivec2 window_size() const {
        return {800, 600};
    }

    // No default icon: unset means the OS/window-manager default is used.
    virtual std::optional<AssetId> window_icon() const {
        return std::nullopt;
    }

    virtual SplashScreen splash_screen() const {
        return {};
    }

    virtual ecs::World& world() = 0;
    virtual void on_start() = 0;
    virtual void on_fixed_update() = 0;
    virtual void on_update() = 0;
    virtual void on_draw() = 0;
    virtual void on_quit() = 0;
};

class GameBase : public IGame {
public:
    ecs::World& world() override {
        return world_;
    }

    void on_start() override {}

    void on_fixed_update() override {
        world_.run(ecs::Schedule::Fixed);
    }

    void on_update() override {
        world_.run(ecs::Schedule::Frame);
    }

    void on_draw() override {}

    void on_quit() override {}

protected:
    ecs::World world_;
};

}
