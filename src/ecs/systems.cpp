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
#include <engine/builtin_ids.h>
#include <engine/render/animation.h>
#include <engine/render/command_buffer.h>
#include <engine/render/particles.h>
#include <engine/render/renderable.h>
#include <engine/render/sprite.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/fatal_error.h>
#include <engine/ui/canvas.h>
#include <engine/ui/document.h>
#include <engine/ui/splash.h>
#include <engine/ui/stylesheet.h>

#include "ui/ui_refs.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <unordered_set>
#include <vector>

namespace engine {
namespace {

void run_input(ecs::World& world) {
    ui::begin_frame(world);
    for (const MouseEvent& event : ecs::EventReader<MouseEvent>{world, world.ctx<ecs::EventCursor<MouseEvent>>()}) {
        ui::UiPointer& pointer = ui::pointer_for(world, event.window);
        if (event.kind == MouseEvent::Kind::Move || event.kind == MouseEvent::Kind::Down ||
                event.kind == MouseEvent::Kind::Up) {
            pointer.position = event.position;
        }
        if (event.kind == MouseEvent::Kind::Down) {
            pointer.down = true;
            ui::handle_pointer(world, event.position.x, event.position.y, event.window);
        } else if (event.kind == MouseEvent::Kind::Up) {
            pointer.down = false;
        } else if (event.kind == MouseEvent::Kind::Move) {
            // Keeps MouseConsumed (SDD §21.4 click-through) current on hover, not just on click —
            // without this, a window that only recomputes it on Down never learns the pointer
            // moved off (or onto) a UI element between clicks.
            ui::update_pointer_hover(world, event.position.x, event.position.y, event.window);
        }
    }
}

// Mirrors the deleted EngineRuntime::tick_loop() splash-aging block: ages every SplashTimer by
// real Time::delta_time and destroys the entity once elapsed crosses total_duration. Calling
// world.destroy() while iterating this view is safe — World::destroy() defers to
// pending_destroy_ for the lifetime of any live View (World::view_depth_), flushed once the view
// backing this range-for goes out of scope, same as every other view-based system in this file.
void run_splash_timers(ecs::World& world) {
    const float dt = world.ctx<Time>().delta_time;
    auto view = world.view<ui::SplashTimer>();
    for (ecs::Entity entity : view) {
        ui::SplashTimer& timer = view.get<ui::SplashTimer>(entity);
        timer.elapsed += dt;
        if (timer.elapsed >= timer.total_duration) {
            world.destroy(entity);
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
    for (const PlaySfxEvent& event :
            ecs::EventReader<PlaySfxEvent>{world, world.ctx<ecs::EventCursor<PlaySfxEvent>>()}) {
        if (deps.assets == nullptr || deps.audio == nullptr) {
            continue;
        }
        auto sound = deps.assets->get<Sound>(event.id);
        deps.audio->play_sfx(*sound, event.volume_scale);
    }
    for (const PlayMusicEvent& event :
            ecs::EventReader<PlayMusicEvent>{world, world.ctx<ecs::EventCursor<PlayMusicEvent>>()}) {
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

struct SpriteMaterialCache {
    std::unordered_map<const render::ITexture*, std::shared_ptr<render::IMaterial>> materials;
};

struct DrawItem {
    int layer = 0;
    int order_in_layer = 0;
    const render::IMaterial* material = nullptr;
    ecs::Entity entity;
    render::Command command;
};

inline bool draw_item_less(const DrawItem& a, const DrawItem& b) {
    if (a.layer != b.layer) {
        return a.layer < b.layer;
    }
    if (a.order_in_layer != b.order_in_layer) {
        return a.order_in_layer < b.order_in_layer;
    }
    if (a.material != b.material) {
        return std::less<>{}(a.material, b.material);
    }
    return a.entity.index < b.entity.index;
}

void run_render(ecs::World& world, const EngineSystemDeps& deps) {
    if (deps.commands == nullptr) {
        return;
    }
    deps.commands->clear();

    auto renderables = world.view<render::Renderable, Transform>();
    auto sprites = world.view<render::Sprite, Transform>();
    auto emitters = world.view<render::ParticleEmitter>();
    if (renderables.begin() == renderables.end() && sprites.begin() == sprites.end() &&
            emitters.begin() == emitters.end()) {
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

    const ui::WindowSize window = ui::window_size_for(world, kPrimaryWindow);
    const glm::mat4 view = view_matrix(*camera_transform);
    const glm::mat4 projection = projection_matrix(*camera, window);

    std::vector<DrawItem> items;

    for (ecs::Entity entity : renderables) {
        const auto& r = renderables.get<render::Renderable>(entity);
        if (!r.mesh || !r.material) {
            report_fatal(deps.fatal, "Renderable is missing mesh or material");
            continue;
        }
        const Transform& transform = world.get<Transform>(entity);
        render::CmdDrawMesh cmd;
        cmd.mesh = r.mesh;
        cmd.material = r.material;
        cmd.model = model_matrix(transform);
        cmd.view = view;
        cmd.projection = projection;
        cmd.color = r.color;
        cmd.uv_scale = {1.0f, 1.0f};
        cmd.uv_offset = {0.0f, 0.0f};

        const render::IMaterial* mat_ptr = r.material.get();
        items.push_back(DrawItem{
                .layer = r.layer,
                .order_in_layer = r.order_in_layer,
                .material = mat_ptr,
                .entity = entity,
                .command = std::move(cmd),
        });
    }

    std::shared_ptr<render::IMesh> default_quad;
    std::shared_ptr<render::IShader> default_shader;
    std::shared_ptr<render::IMaterial> default_unlit;
    if (deps.assets != nullptr) {
        if (auto mesh_res = deps.assets->try_get<render::IMesh>(builtin::mesh_quad)) {
            default_quad = std::move(*mesh_res);
        }
        if (auto shader_res = deps.assets->try_get<render::IShader>(builtin::shader_unlit)) {
            default_shader = std::move(*shader_res);
        }
        if (auto mat_res = deps.assets->try_get<render::IMaterial>(builtin::material_unlit)) {
            default_unlit = std::move(*mat_res);
        }
    }

    auto& cache = world.ctx<SpriteMaterialCache>();

    for (ecs::Entity entity : sprites) {
        if (world.try_get<render::Renderable>(entity) != nullptr) {
            continue;
        }
        const auto& s = sprites.get<render::Sprite>(entity);
        std::shared_ptr<render::IMesh> mesh = s.mesh ? s.mesh : default_quad;
        std::shared_ptr<render::IMaterial> material;
        if (s.material != nullptr) {
            material = s.material;
        } else if (s.texture != nullptr) {
            auto it = cache.materials.find(s.texture.get());
            if (it != cache.materials.end()) {
                material = it->second;
            } else if (default_shader != nullptr) {
                material = std::make_shared<render::Material>(
                        default_shader, s.texture, glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}, render::BlendMode::Alpha);
                cache.materials[s.texture.get()] = material;
            }
        } else {
            material = default_unlit;
        }

        if (!mesh || !material) {
            report_fatal(deps.fatal, "Sprite is missing mesh or material");
            continue;
        }
        const Transform& transform = world.get<Transform>(entity);
        glm::mat4 model = model_matrix(transform);
        if (s.pixel_size.x > 0.0f && s.pixel_size.y > 0.0f && s.pixels_per_unit > 0.0f) {
            const glm::vec2 world_size = s.pixel_size / s.pixels_per_unit;
            const glm::vec3 pivot_offset{(0.5f - s.pivot.x) * world_size.x, (0.5f - s.pivot.y) * world_size.y, 0.0f};
            model = glm::translate(model, pivot_offset);
            model = glm::scale(model, glm::vec3{world_size.x, world_size.y, 1.0f});
        }
        if (s.flip_x || s.flip_y) {
            model = glm::scale(model, glm::vec3{s.flip_x ? -1.0f : 1.0f, s.flip_y ? -1.0f : 1.0f, 1.0f});
        }

        render::CmdDrawMesh cmd;
        cmd.mesh = std::move(mesh);
        cmd.material = material;
        cmd.model = model;
        cmd.view = view;
        cmd.projection = projection;
        cmd.color = s.color;
        cmd.uv_scale = s.tiling;
        cmd.uv_offset = s.offset;

        const render::IMaterial* mat_ptr = material.get();
        items.push_back(DrawItem{
                .layer = s.layer,
                .order_in_layer = s.order_in_layer,
                .material = mat_ptr,
                .entity = entity,
                .command = std::move(cmd),
        });
    }

    for (ecs::Entity entity : emitters) {
        const auto& em = emitters.get<render::ParticleEmitter>(entity);
        if (em.particles.empty()) {
            continue;
        }
        std::shared_ptr<render::IMesh> mesh = em.mesh ? em.mesh : default_quad;
        std::shared_ptr<render::IMaterial> material;
        render::BlendMode blend = em.blend;
        if (em.material != nullptr) {
            material = em.material;
            blend = em.material->blend();
        } else if (em.texture != nullptr) {
            auto it = cache.materials.find(em.texture.get());
            if (it != cache.materials.end()) {
                material = it->second;
            } else if (default_shader != nullptr) {
                material = std::make_shared<render::Material>(
                        default_shader, em.texture, glm::vec4{1.0f, 1.0f, 1.0f, 1.0f}, em.blend);
                cache.materials[em.texture.get()] = material;
            }
        } else {
            material = default_unlit;
        }

        if (!mesh || !material) {
            report_fatal(deps.fatal, "ParticleEmitter is missing mesh or material");
            continue;
        }

        glm::mat4 model{1.0f};
        if (em.simulation_space == render::SimulationSpace::Local) {
            if (const auto* transform = world.try_get<Transform>(entity)) {
                model = model_matrix(*transform);
            }
        }

        render::CmdDrawParticles cmd;
        cmd.mesh = std::move(mesh);
        cmd.material = material;
        cmd.view = view;
        cmd.projection = projection;
        cmd.blend = blend;
        cmd.instances.reserve(em.particles.size());

        for (const auto& p : em.particles) {
            glm::vec3 pos = p.position;
            if (em.simulation_space == render::SimulationSpace::Local) {
                pos = glm::vec3(model * glm::vec4(p.position, 1.0f));
            }
            cmd.instances.push_back(render::ParticleInstance{
                    .position = pos,
                    .rotation = p.rotation,
                    .size = p.size,
                    .color = p.color,
                    .uv_scale = em.uv_scale,
                    .uv_offset = em.uv_offset,
            });
        }

        const render::IMaterial* mat_ptr = material.get();
        items.push_back(DrawItem{
                .layer = em.layer,
                .order_in_layer = em.order_in_layer,
                .material = mat_ptr,
                .entity = entity,
                .command = std::move(cmd),
        });
    }

    std::stable_sort(items.begin(), items.end(), draw_item_less);

    for (DrawItem& item : items) {
        deps.commands->push(std::move(item.command));
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
    WindowId window = kPrimaryWindow;
};

void run_ui_render(ecs::World& world, const EngineSystemDeps& deps) {
    if (deps.commands == nullptr) {
        return;
    }

    const Time& time = world.ctx<Time>();
    std::vector<CanvasDraw> canvases;
    {
        auto view = world.view<ui::UiCanvas>();
        for (ecs::Entity entity : view) {
            ui::UiCanvas& canvas = view.get<ui::UiCanvas>(entity);
            CanvasDraw draw{
                    canvas.order, entity.index, canvas.rect, canvas.fit, canvas.reference_size, nullptr, nullptr, canvas.window};
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
    // Tracks which non-primary CommandBuffers this call has already cleared — a target window's
    // buffer needs clearing once per frame before anything is pushed into it (the primary's is
    // already cleared by run_render), and this set is function-local so it naturally resets every
    // invocation with no state to carry across frames.
    std::unordered_set<WindowId> cleared_windows;
    for (const CanvasDraw& canvas : canvases) {
        const ui::UiPointer& pointer = ui::pointer_for(world, canvas.window);
        const ui::WindowSize size = ui::window_size_for(world, canvas.window);
        const ui::UiCanvasSpace space = ui::canvas_layout_space(canvas.rect, canvas.fit, canvas.reference_size);
        const float window_width = space.reference_space ? space.layout_rect.w : static_cast<float>(size.width);
        const float window_height = space.reference_space ? space.layout_rect.h : static_cast<float>(size.height);
        render::CommandBuffer* target = deps.commands;
        if (canvas.window != kPrimaryWindow) {
            target = deps.commands_for_window ? deps.commands_for_window(canvas.window) : nullptr;
            if (target == nullptr) {
                continue;
            }
            if (cleared_windows.insert(canvas.window).second) {
                target->clear();
            }
        }

        // Lazily loads into this canvas's window atlas whatever its document/stylesheet actually
        // reference, instead of the old Engine::init behavior of preloading the entire asset
        // catalog up front (see ui/ui_refs.h). builtin::font_ui is ensured unconditionally — it's
        // the fallback for any element with no font-family at all.
        if (canvas.document != nullptr) {
            if (deps.ensure_ui_font) {
                deps.ensure_ui_font(canvas.window, builtin::font_ui);
                for (const AssetId id : ui::collect_referenced_fonts(*canvas.document, canvas.stylesheet)) {
                    deps.ensure_ui_font(canvas.window, id);
                }
            }
            if (deps.ensure_ui_image) {
                for (const AssetId id : ui::collect_referenced_images(*canvas.document, canvas.stylesheet)) {
                    deps.ensure_ui_image(canvas.window, id);
                }
            }
        }

        target->push(render::CmdDrawUI{
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

void run_sprite_animations(ecs::World& world) {
    const float dt = world.ctx<Time>().delta_time;
    auto view = world.view<render::SpriteAnimator, render::Sprite>();
    for (ecs::Entity entity : view) {
        auto& anim = view.get<render::SpriteAnimator>(entity);
        auto& sprite = view.get<render::Sprite>(entity);

        if (!anim.playing || !anim.clip || anim.clip->frames.empty() || anim.speed <= 0.0f) {
            continue;
        }

        const auto& frames = anim.clip->frames;
        if (anim.current_frame >= frames.size()) {
            anim.current_frame = 0;
            anim.elapsed = 0.0f;
        }

        anim.elapsed += dt * anim.speed;

        while (anim.elapsed >= frames[anim.current_frame].duration) {
            anim.elapsed -= frames[anim.current_frame].duration;
            anim.current_frame++;
            if (anim.current_frame >= frames.size()) {
                if (anim.clip->loop) {
                    anim.current_frame = 0;
                } else {
                    anim.current_frame = frames.size() - 1;
                    anim.playing = false;
                    break;
                }
            }
        }

        const auto& current_frame = frames[anim.current_frame];
        sprite.texture = current_frame.sprite.texture;
        sprite.tiling = current_frame.sprite.tiling;
        sprite.offset = current_frame.sprite.offset;
        sprite.pixel_size = current_frame.sprite.pixel_size;
        sprite.pixels_per_unit = current_frame.sprite.pixels_per_unit;
        sprite.pivot = current_frame.sprite.pivot;
    }
}

void run_particles(ecs::World& world) {
    const float dt = world.ctx<Time>().delta_time;
    auto view = world.view<render::ParticleEmitter>();

    bool any_collisions = false;
    for (ecs::Entity entity : view) {
        if (view.get<render::ParticleEmitter>(entity).collision_enabled) {
            any_collisions = true;
            break;
        }
    }

    std::vector<render::ParticleCollider> colliders;
    if (any_collisions) {
        world.view<Transform, BoxCollider>().each([&](const Transform& transform, const BoxCollider& box) {
            if (!box.is_trigger) {
                colliders.push_back(render::ParticleCollider{
                        .shape = render::ParticleCollider::Shape::Box,
                        .position = transform.position,
                        .box_size = box.size,
                        .circle_radius = 0.0f,
                        .layer = box.layer,
                });
            }
        });
        world.view<Transform, CircleCollider>().each([&](const Transform& transform, const CircleCollider& circle) {
            if (!circle.is_trigger) {
                colliders.push_back(render::ParticleCollider{
                        .shape = render::ParticleCollider::Shape::Circle,
                        .position = transform.position,
                        .box_size = glm::vec3{0.0f},
                        .circle_radius = circle.radius,
                        .layer = circle.layer,
                });
            }
        });
    }

    for (ecs::Entity entity : view) {
        auto& emitter = view.get<render::ParticleEmitter>(entity);
        glm::mat4 model{1.0f};
        if (const auto* transform = world.try_get<Transform>(entity)) {
            model = model_matrix(*transform);
        }
        render::update_emitter(emitter, dt, model, colliders);
    }
}

void register_engine_systems(ecs::World& world, EngineSystemDeps deps) {
    world.ctx<EngineSystemsRegistered>().value = true;

    world.add_system(ecs::Schedule::Fixed, ecs::Phase::Physics, [](ecs::World& w) { run_physics(w); });
    world.add_system(ecs::Schedule::Frame, ecs::Phase::Input, [](ecs::World& w) { run_input(w); });
    world.add_system(ecs::Schedule::Frame, ecs::Phase::Input, [](ecs::World& w) { run_splash_timers(w); });
    world.add_system(ecs::Schedule::Frame, ecs::Phase::Game, [](ecs::World& w) { run_sprite_animations(w); });
    world.add_system(ecs::Schedule::Frame, ecs::Phase::Game, [](ecs::World& w) { run_particles(w); });
    world.add_system(ecs::Schedule::Frame, ecs::Phase::Bind, [deps](ecs::World& w) { run_bind(w, deps); });
    world.add_system(ecs::Schedule::Frame, ecs::Phase::Audio, [deps](ecs::World& w) { run_audio(w, deps); });
    world.add_system(ecs::Schedule::Frame, ecs::Phase::Render, [deps](ecs::World& w) { run_render(w, deps); });
    world.add_system(ecs::Schedule::Frame, ecs::Phase::UiRender, [deps](ecs::World& w) { run_ui_render(w, deps); });
}

}
