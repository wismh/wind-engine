#pragma once

#include <engine/ecs/schedule.h>
#include <engine/ecs/world.h>

#include <glm/vec2.hpp>

#include <string>

namespace engine {

class IGame {
public:
    virtual ~IGame() = default;

    virtual std::string WindowTitle() const {
        return "Game";
    }

    virtual glm::ivec2 WindowSize() const {
        return {800, 600};
    }

    virtual ecs::World& World() = 0;
    virtual void OnStart() = 0;
    virtual void OnFixedUpdate() = 0;
    virtual void OnUpdate() = 0;
    virtual void OnDraw() = 0;
    virtual void OnQuit() = 0;
};

class GameBase : public IGame {
public:
    ecs::World& World() override {
        return world_;
    }

    void OnStart() override {}

    void OnFixedUpdate() override {
        world_.Run(ecs::Schedule::Fixed);
    }

    void OnUpdate() override {
        world_.Run(ecs::Schedule::Frame);
    }

    void OnDraw() override {}

    void OnQuit() override {}

protected:
    ecs::World world_;
};

}
