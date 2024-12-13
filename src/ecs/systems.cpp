#include <engine/ecs/systems.h>

#include <engine/audio/audio_system.h>
#include <engine/audio/events.h>
#include <engine/audio/sound.h>
#include <engine/core/input_system.h>
#include <engine/core/time.h>
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

bool instance_needs_rebuild(const ui::UiInstance* instance, const ui::UiCanvas& canvas) {
    if (instance == nullptr) {
        return true;
    }
    return instance->loaded_document != canvas.document || instance->loaded_stylesheet != canvas.stylesheet ||
            instance->loaded_extra_stylesheets != canvas.extra_stylesheets ||
            instance->loaded_data_context != canvas.data_context.get();
}

void clone_document(ecs::World& world, ecs::Entity entity, ui::UiCanvas& canvas, AssetsDb& assets,
        ui::UiInstance*& instance) {
    const std::shared_ptr<ui::UiDocument> document = assets.get<ui::UiDocument>(canvas.document);
    ui::UiInstance fresh;
    fresh.document = *document;
    fresh.loaded_document = canvas.document;
    fresh.loaded_stylesheet = canvas.stylesheet;
    fresh.loaded_extra_stylesheets = canvas.extra_stylesheets;
    fresh.loaded_data_context = canvas.data_context.get();
    if (instance == nullptr) {
        instance = &world.emplace<ui::UiInstance>(entity, std::move(fresh));
    } else {
        *instance = std::move(fresh);
    }
}

std::vector<AssetId> resolved_stylesheet_ids(const ui::UiCanvas& canvas, const ui::UiInstance& instance) {
    std::vector<AssetId> ids;
    if (instance.document.stylesheet) {
        ids.push_back(*instance.document.stylesheet);
    }
    if (canvas.stylesheet) {
        ids.push_back(*canvas.stylesheet);
    }
    ids.insert(ids.end(), canvas.extra_stylesheets.begin(), canvas.extra_stylesheets.end());
    return ids;
}

void load_merged_stylesheets(ui::UiInstance& instance, const ui::UiCanvas& canvas, AssetsDb& assets) {
    const std::vector<AssetId> wanted = resolved_stylesheet_ids(canvas, instance);
    if (instance.loaded_sheet_ids == wanted) {
        return;
    }
    ui::Stylesheet merged;
    bool all_found = true;
    for (const AssetId& id : wanted) {
        if (auto sheet = assets.try_get<ui::Stylesheet>(id)) {
            merged.rules.insert(merged.rules.end(), (*sheet)->rules.begin(), (*sheet)->rules.end());
        } else {
            all_found = false;
        }
    }
    if (wanted.empty()) {
        instance.stylesheet.reset();
    } else {
        instance.stylesheet = std::move(merged);
    }
    if (all_found) {
        instance.loaded_sheet_ids = wanted;
    }
}

void run_bind(ecs::World& world, const EngineSystemDeps& deps) {
    auto view = world.view<ui::UiCanvas>();
    for (ecs::Entity entity : view) {
        ui::UiCanvas& canvas = view.get<ui::UiCanvas>(entity);
        ui::UiInstance* instance = world.try_get<ui::UiInstance>(entity);
        if (deps.assets != nullptr && instance_needs_rebuild(instance, canvas)) {
            clone_document(world, entity, canvas, *deps.assets, instance);
        }
        if (!canvas.data_context) {
            continue;
        }
        if (instance == nullptr) {
            continue;
        }
        (void) ui::apply_bindings(instance->document, *canvas.data_context);
        if (deps.assets == nullptr) {
            continue;
        }
        load_merged_stylesheets(*instance, canvas, *deps.assets);
    }
}

void run_audio(ecs::World& world, const EngineSystemDeps& deps) {
    for (const PlaySfxEvent& event : ecs::EventReader<PlaySfxEvent>{world}) {
        if (deps.assets == nullptr || deps.audio == nullptr) {
            continue;
        }
        auto sound = deps.assets->get<Sound>(event.id);
        deps.audio->play_sfx(*sound, event.volume_scale);
    }
    for (const PlayMusicEvent& event : ecs::EventReader<PlayMusicEvent>{world}) {
        if (deps.assets == nullptr || deps.audio == nullptr) {
            continue;
        }
        auto sound = deps.assets->get<Sound>(event.id);
        deps.audio->play_music(*sound, event.loop, event.fade_seconds);
    }
}

glm::mat4 model_matrix(const Transform& transform) {
    glm::mat4 model(1.0f);
    model = glm::translate(model, transform.position);
    model = glm::rotate(model, transform.rotation.x, glm::vec3{1.0f, 0.0f, 0.0f});
    model = glm::rotate(model, transform.rotation.y, glm::vec3{0.0f, 1.0f, 0.0f});
    model = glm::rotate(model, transform.rotation.z, glm::vec3{0.0f, 0.0f, 1.0f});
    model = glm::scale(model, transform.scale);
    return model;
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

    auto entities = world.view<render::Renderable, Transform>();
    if (entities.begin() == entities.end()) {
        return;
    }

    const ActiveCamera& active = world.ctx<ActiveCamera>();
    if (!world.valid(active.entity)) {
        return;
    }

    const Camera* camera = world.try_get<Camera>(active.entity);
    const Transform* camera_transform = world.try_get<Transform>(active.entity);
    if (camera == nullptr || camera_transform == nullptr) {
        report_fatal(deps.fatal, "ActiveCamera is missing Camera or Transform");
        return;
    }

    const ui::WindowSize& window = world.ctx<ui::WindowSize>();
    const glm::mat4 view = view_matrix(*camera_transform);
    const glm::mat4 projection = projection_matrix(*camera, window);

    std::vector<render::RenderableItem> items;
    for (ecs::Entity entity : entities) {
        items.push_back(render::RenderableItem{entities.get<render::Renderable>(entity), entity});
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
        cmd.model = model_matrix(transform);
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
    ui::UiFit fit = ui::UiFit::FillWindow;
    glm::vec2 reference_size{0.0f, 0.0f};
    ui::UiDocument* document = nullptr;
    const ui::Stylesheet* stylesheet = nullptr;
};

void run_ui_render(ecs::World& world, const EngineSystemDeps& deps) {
    if (deps.commands == nullptr) {
        return;
    }

    const ui::UiPointer& pointer = world.ctx<ui::UiPointer>();
    const Time& time = world.ctx<Time>();
    const ui::WindowSize& window = world.ctx<ui::WindowSize>();
    std::vector<CanvasDraw> canvases;
    {
        auto view = world.view<ui::UiCanvas>();
        for (ecs::Entity entity : view) {
            ui::UiCanvas& canvas = view.get<ui::UiCanvas>(entity);
            CanvasDraw draw{canvas.order, entity.index, canvas.rect, canvas.fit, canvas.reference_size};
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
        const ui::UiCanvasSpace space = ui::canvas_layout_space(canvas.rect, canvas.fit, canvas.reference_size);
        const float window_width = space.reference_space ? space.layout_rect.w : static_cast<float>(window.width);
        const float window_height = space.reference_space ? space.layout_rect.h : static_cast<float>(window.height);
        deps.commands->push(render::CmdDrawUI{
                space.layout_rect,
                canvas.document,
                canvas.stylesheet,
                (pointer.position - space.offset) / space.scale,
                pointer.down,
                time.delta_time,
                window_width,
                window_height,
                space.offset,
                space.scale,
        });
    }
}

}

void register_engine_systems(ecs::World& world, EngineSystemDeps deps) {
    world.ctx<EngineSystemsRegistered>().value = true;

    world.add_system(ecs::Schedule::Fixed, ecs::Phase::Physics, [](ecs::World& w) { run_physics(w); });
    world.add_system(ecs::Schedule::Frame, ecs::Phase::Input, [](ecs::World& w) { run_input(w); });
    world.add_system(ecs::Schedule::Frame, ecs::Phase::Bind, [deps](ecs::World& w) { run_bind(w, deps); });
    world.add_system(ecs::Schedule::Frame, ecs::Phase::Audio, [deps](ecs::World& w) { run_audio(w, deps); });
    world.add_system(ecs::Schedule::Frame, ecs::Phase::Render, [deps](ecs::World& w) { run_render(w, deps); });
    world.add_system(ecs::Schedule::Frame, ecs::Phase::UiRender, [deps](ecs::World& w) { run_ui_render(w, deps); });
}

}
