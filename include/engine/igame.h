#pragma once

#include <engine/ecs/schedule.h>
#include <engine/ecs/world.h>

#include <glm/vec2.hpp>

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
