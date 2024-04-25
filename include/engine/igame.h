#pragma once

#include <engine/ecs/schedule.h>
#include <engine/ecs/world.h>
#include <engine/resources/asset_id.h>

#include <glm/vec2.hpp>

#include <optional>
#include <string>

namespace engine {

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
