#include <engine/ecs/systems.h>

#include <engine/audio/audio_system.h>
#include <engine/audio/events.h>
#include <engine/audio/sound.h>
#include <engine/core/input_system.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/events.h>
#include <engine/ecs/physics.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/transform.h>
#include <engine/render/command_buffer.h>
#include <engine/render/renderable.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/fatal_error.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/stylesheet.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace engine {
namespace {

void run_input(ecs::World& world) {
    ui::begin_frame(world);
    ui::UiPointer& pointer = world.ctx<ui::UiPointer>();
    for (const MouseEvent& event : ecs::EventReader<MouseEvent>{world}) {
        if (event.kind == MouseEvent::Kind::Move || event.kind == MouseEvent::Kind::Down ||
                event.kind == MouseEvent::Kind::Up) {
            pointer.position = event.position;
        }
        if (event.kind == MouseEvent::Kind::Down) {
            pointer.down = true;
            ui::handle_pointer(world, event.position.x, event.position.y);
        } else if (event.kind == MouseEvent::Kind::Up) {
            pointer.down = false;
        }
    }
}

void run_bind(ecs::World& world, const EngineSystemDeps& deps) {
    auto view = world.view<ui::UiCanvas>();
    for (ecs::Entity entity : view) {
        ui::UiCanvas& canvas = view.get<ui::UiCanvas>(entity);
        if (!canvas.data_context) {
            continue;
        }
        ui::UiInstance* instance = world.try_get<ui::UiInstance>(entity);
        if (instance == nullptr) {
            continue;
        }
        (void) ui::apply_bindings(instance->document, *canvas.data_context);
        if (deps.assets == nullptr || instance->stylesheet) {
            continue;
        }
        std::optional<AssetId> sheet_id = canvas.stylesheet;
        if (!sheet_id) {
            sheet_id = instance->document.stylesheet;
        }
        if (!sheet_id) {
            continue;
        }
        if (auto sheet = deps.assets->TryGet<ui::Stylesheet>(*sheet_id)) {
            instance->stylesheet = **sheet;
        }
    }
}

void run_audio(ecs::World& world, const EngineSystemDeps& deps) {
    for (const PlaySfxEvent& event : ecs::EventReader<PlaySfxEvent>{world}) {
        if (deps.assets == nullptr || deps.audio == nullptr) {
            continue;
        }
        auto sound = deps.assets->TryGet<Sound>(event.id);
        if (!sound) {
            continue;
        }
        deps.audio->PlaySfx(**sound, event.volume_scale);
    }
    for (const PlayMusicEvent& event : ecs::EventReader<PlayMusicEvent>{world}) {
        if (deps.assets == nullptr || deps.audio == nullptr) {
            continue;
        }
        auto sound = deps.assets->TryGet<Sound>(event.id);
        if (!sound) {
            continue;
        }
        deps.audio->PlayMusic(**sound, event.loop, event.fade_seconds);
    }
}

void report_fatal(IFatalError* fatal, std::string_view message) {
    if (fatal != nullptr) {
        fatal->report(message);
    }
}

void run_render(ecs::World& world, const EngineSystemDeps& deps) {
    if (deps.commands == nullptr) {
        return;
    }
    deps.commands->clear();

    const ActiveCamera& active = world.ctx<ActiveCamera>();
    const Camera* camera = world.valid(active.entity) ? world.try_get<Camera>(active.entity) : nullptr;
    const Transform* camera_transform =
            world.valid(active.entity) ? world.try_get<Transform>(active.entity) : nullptr;
    if (camera == nullptr || camera_transform == nullptr) {
        report_fatal(deps.fatal, "ActiveCamera is missing Camera or Transform");
        return;
    }

    const ui::WindowSize& window = world.ctx<ui::WindowSize>();
    const glm::mat4 view = view_matrix(*camera_transform);
    const glm::mat4 projection = projection_matrix(*camera, window);

    std::vector<render::RenderableItem> items;
    {
        auto entities = world.view<render::Renderable, Transform>();
        for (ecs::Entity entity : entities) {
            items.push_back(render::RenderableItem{entities.get<render::Renderable>(entity), entity});
        }
    }
    render::sort_renderables(items);

    for (const render::RenderableItem& item : items) {
        if (!item.renderable.mesh || !item.renderable.material) {
            report_fatal(deps.fatal, "Renderable is missing mesh or material");
            continue;
        }
        const Transform& transform = world.get<Transform>(item.entity);
        render::CmdDrawMesh cmd;
        cmd.mesh = item.renderable.mesh;
        cmd.material = item.renderable.material;
        cmd.model = glm::translate(glm::mat4(1.0f), transform.position);
        cmd.view = view;
        cmd.projection = projection;
        cmd.color = item.renderable.color;
        deps.commands->push(std::move(cmd));
    }
}

struct CanvasDraw {
    int order = 0;
    std::uint32_t index = 0;
    render::Rect rect{};
    ui::UiDocument* document = nullptr;
    const ui::Stylesheet* stylesheet = nullptr;
};

void run_ui_render(ecs::World& world, const EngineSystemDeps& deps) {
    if (deps.commands == nullptr) {
        return;
    }

    const ui::UiPointer& pointer = world.ctx<ui::UiPointer>();
    std::vector<CanvasDraw> canvases;
    {
        auto view = world.view<ui::UiCanvas>();
        for (ecs::Entity entity : view) {
            ui::UiCanvas& canvas = view.get<ui::UiCanvas>(entity);
            CanvasDraw draw{canvas.order, entity.index, canvas.rect};
            if (ui::UiInstance* instance = world.try_get<ui::UiInstance>(entity)) {
                draw.document = &instance->document;
                if (instance->stylesheet) {
                    draw.stylesheet = &*instance->stylesheet;
                }
            }
            canvases.push_back(draw);
        }
    }
    std::stable_sort(canvases.begin(), canvases.end(), [](const CanvasDraw& a, const CanvasDraw& b) {
        if (a.order != b.order) {
            return a.order < b.order;
        }
        return a.index < b.index;
    });
    for (const CanvasDraw& canvas : canvases) {
        deps.commands->push(render::CmdDrawUI{
                canvas.rect,
                canvas.document,
                canvas.stylesheet,
                pointer.position,
                pointer.down,
        });
    }
}

}

void RegisterEngineSystems(ecs::World& world, EngineSystemDeps deps) {
    world.ctx<EngineSystemsRegistered>().value = true;

    world.AddSystem(ecs::Schedule::Fixed, ecs::Phase::Physics, [](ecs::World& w) { run_physics(w); });
    world.AddSystem(ecs::Schedule::Frame, ecs::Phase::Input, [](ecs::World& w) { run_input(w); });
    world.AddSystem(ecs::Schedule::Frame, ecs::Phase::Bind, [deps](ecs::World& w) { run_bind(w, deps); });
    world.AddSystem(ecs::Schedule::Frame, ecs::Phase::Audio, [deps](ecs::World& w) { run_audio(w, deps); });
    world.AddSystem(ecs::Schedule::Frame, ecs::Phase::Render, [deps](ecs::World& w) { run_render(w, deps); });
    world.AddSystem(ecs::Schedule::Frame, ecs::Phase::UiRender, [deps](ecs::World& w) { run_ui_render(w, deps); });
}

}
