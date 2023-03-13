#include <engine/ecs/systems.h>

#include <engine/ecs/schedule.h>
#include <engine/ui/canvas.h>

namespace engine {

void RegisterEngineSystems(ecs::World& world) {
    world.ctx<EngineSystemsRegistered>().value = true;

    world.AddSystem(ecs::Schedule::Fixed, ecs::Phase::Physics, [](ecs::World&) {});
    world.AddSystem(ecs::Schedule::Frame, ecs::Phase::Input, [](ecs::World& w) { ui::begin_frame(w); });
    world.AddSystem(ecs::Schedule::Frame, ecs::Phase::Bind, [](ecs::World&) {});
    world.AddSystem(ecs::Schedule::Frame, ecs::Phase::Audio, [](ecs::World&) {});
    world.AddSystem(ecs::Schedule::Frame, ecs::Phase::Render, [](ecs::World&) {});
    world.AddSystem(ecs::Schedule::Frame, ecs::Phase::UiRender, [](ecs::World&) {});
}

}
