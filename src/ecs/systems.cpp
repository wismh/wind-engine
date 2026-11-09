#include <engine/ecs/systems.h>

#include <engine/audio/audio_listener.h>
#include <engine/audio/audio_source_3d.h>
#include <engine/audio/audio_system.h>
#include <engine/audio/events.h>
#include <engine/audio/sound.h>
#include <engine/builtin_ids.h>
#include <engine/core/bound_windows.h>
#include <engine/core/input_system.h>
#include <engine/core/job_system.h>
#include <engine/core/time.h>
#include <engine/ecs/camera.h>
#include <engine/ecs/events.h>
#include <engine/ecs/hierarchy.h>
#include <engine/ecs/model.h>
#include <engine/ecs/physics_2d.h>
#include <engine/ecs/physics_3d.h>
#include <engine/ecs/schedule.h>
#include <engine/ecs/transform.h>
#include <engine/loc/catalog.h>
#include <engine/log.h>
#include <engine/animation/tween.h>
#include <engine/nav/nav_system.h>
#include <engine/render/animation.h>
#include <engine/render/decal.h>
#include <engine/render/command_buffer.h>
#include <engine/render/draw_sort.h>
#include <engine/render/fog.h>
#include <engine/render/frustum.h>
#include <engine/render/lights.h>
#include <engine/render/particles.h>
#include <engine/render/post_process.h>
#include <engine/render/model.h>
#include <engine/render/renderable.h>
#include <engine/render/sprite.h>
#include <engine/render/tilemap.h>
#include <engine/resources/assets_db.h>
#include <engine/resources/default_fatal_error.h>
#include <engine/resources/fatal_error.h>
#include <engine/ui/canvas.h>
#include <engine/ui/presentation.h>
#include <engine/ui/document.h>
#include <engine/ui/inspector.h>
#include <engine/ui/splash.h>
#include <engine/ui/stylesheet.h>

#include "ui/dock_runtime.h"
#include "ui/input_batch.h"
#include "ui/popup.h"
#include "ui/profile.h"
#include "ui/pseudo_warning.h"
#include "ui/svg_attach.h"
#include "ui/ui_refs.h"

#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace engine {

    void run_model_animations(ecs::World &world);

    namespace {

        void run_input(ecs::World &world) {
            ui::begin_frame(world);
            // Scoped to exactly this run_input() call (wind ui_scrollview_perf_plan.md Крок 4) — several
            // MouseEvents below can independently hit the same UiCanvas (e.g. a few Move events, or Wheel
            // events during an active scroll), and nothing changes ViewModel data between them (no
            // Game-phase system runs mid-loop), so the *_for_run_input() calls reuse one canvas's
            // bind+style+layout across repeated touches instead of redoing it per event. See
            // src/ui/input_batch.h for why this is a stack-local cache and not ctx<>() state.
            ui::UiInputBatchCache input_batch;
            for (const MouseEvent &event:
                 ecs::EventReader<MouseEvent>{world, world.ctx<ecs::EventCursor<MouseEvent>>()}) {
                // Relative mouse mode hides the cursor: no hover, press, or wheel at a point the player cannot
                // see, and so no consumption. Up still ends a press, drag, or pan that began before the lock.
                if (event.locked && event.kind != MouseEvent::Kind::Up) {
                    continue;
                }
                ui::UiPointer &pointer = ui::pointer_for(world, event.window);
                if (event.kind == MouseEvent::Kind::Move || event.kind == MouseEvent::Kind::Down ||
                    event.kind == MouseEvent::Kind::Up) {
                    pointer.position = event.position;
                }
                if (event.kind == MouseEvent::Kind::Down) {
                    // The cursor under the press is the one the press keeps, even when no frame resolved it
                    // since the pointer got there.
                    (void) ui::update_cursor(world, event.window);
                    pointer.down = true;
                    ui::handle_pointer_for_run_input(world, event.position.x, event.position.y, event.window,
                                                     input_batch, event.button == MouseButton::Left, event.clicks);
                } else if (event.kind == MouseEvent::Kind::Up) {
                    pointer.down = false;
                    ui::end_drag(world, event.window);
                    ui::end_pan(world, event.window);
                } else if (event.kind == MouseEvent::Kind::Move) {
                    // Keeps MouseConsumed current on hover, not just on click —
                    // without this, a window that only recomputes it on Down never learns the pointer
                    // moved off (or onto) a UI element between clicks.
                    ui::update_pointer_hover_for_run_input(world, event.position.x, event.position.y, event.window,
                                                           input_batch);
                    // Keeps an in-progress `drag="{binding}"` tracking the pointer even once it has left
                    // the dragged element's bounds (real drag UX) — a no-op when no drag is active.
                    ui::update_drag_for_run_input(world, event.position.x, event.position.y, event.window, input_batch);
                    ui::update_pan_for_run_input(world, event.position.x, event.position.y, event.window, input_batch);
                    // Extends a TextInput or selectable-Label drag-select while the button stays down.
                    // No batch needed: it never touches bindings/layout, only the focused Element.
                    ui::update_text_selection(world, event.position.x, event.position.y, event.window);
                } else if (event.kind == MouseEvent::Kind::Wheel) {
                    ui::handle_wheel_for_run_input(world, event.position.x, event.position.y, event.wheel_y,
                                                   event.window, input_batch);
                }
            }
            for (const KeyEvent &event: ecs::EventReader<KeyEvent>{world, world.ctx<ecs::EventCursor<KeyEvent>>()}) {
                ui::handle_key(world, event.key, event.down, event.repeat, event.window);
            }
            // An IME commit is an empty TEXT_EDITING event, then TEXT_INPUT. Clear the preedit first.
            for (const TextEditingEvent &event:
                 ecs::EventReader<TextEditingEvent>{world, world.ctx<ecs::EventCursor<TextEditingEvent>>()}) {
                ui::handle_text_editing(world, event.text, event.start, event.length, event.window);
            }
            for (const TextInputEvent &event:
                 ecs::EventReader<TextInputEvent>{world, world.ctx<ecs::EventCursor<TextInputEvent>>()}) {
                ui::handle_text_input(world, event.text, event.window);
            }
        }

        // Ages every SplashTimer by real Time::delta_time and destroys the entity once elapsed crosses
        // total_duration. Calling world.destroy() while iterating this view is safe — World::destroy() defers to
        // pending_destroy_ for the lifetime of any live View (World::view_depth_), flushed once the view
        // backing this range-for goes out of scope, same as every other view-based system in this file.
        void run_splash_timers(ecs::World &world) {
            const float dt = world.ctx<Time>().delta_time;
            auto view = world.view<ui::SplashTimer>();
            for (ecs::Entity entity: view) {
                ui::SplashTimer &timer = view.get<ui::SplashTimer>(entity);
                timer.elapsed += dt;
                if (timer.elapsed >= timer.total_duration) {
                    world.destroy(entity);
                }
            }
        }

        // Logs each bind error of one canvas once: run_bind binds every frame, and a missing binding fails every frame.
        class BindErrorLog final : public ui::IBindErrorSink {
        public:
            BindErrorLog(const ui::UiCanvas &canvas, ui::UiInstance &instance)
                : canvas_(canvas)
                , instance_(instance) {}

            void bind_error(std::string_view message) override {
                if (!instance_.reported_bind_errors.emplace(message).second) {
                    return;
                }
                const std::string document = canvas_.document ? std::string(canvas_.document->hex()) : "(none)";
                log::warn("UI document " + document + ": " + std::string(message));
            }

        private:
            const ui::UiCanvas &canvas_;
            ui::UiInstance &instance_;
        };

        bool instance_needs_rebuild(const ui::UiInstance *instance, const ui::UiCanvas &canvas) {
            if (!canvas.document.has_value()) {
                return false;
            }
            if (instance == nullptr) {
                return true;
            }
            return instance->loaded_document != canvas.document;
        }

        void clone_document(ecs::World &world, ecs::Entity entity, ui::UiCanvas &canvas, AssetsDb &assets,
                            ui::UiInstance *&instance) {
            const std::shared_ptr<ui::UiDocument> document = assets.get<ui::UiDocument>(*canvas.document);
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

        std::vector<AssetId> resolved_stylesheet_ids(const ui::UiCanvas &canvas, const ui::UiInstance &instance) {
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

        // A stylesheet the canvas names but the catalog does not hold is reported once per world and id. It is still
        // looked up every frame, because a catalog loaded later (a mod) can make it appear.
        struct MissingStylesheets {
            std::set<AssetId> warned;
        };

        void warn_missing_stylesheet(ecs::World &world, const AssetId &id) {
            if (world.ctx<MissingStylesheets>().warned.insert(id).second) {
                log::warn("UI stylesheet " + std::string(id.hex()) + " is not in the asset catalog");
            }
        }

        void load_merged_stylesheets(ecs::World &world, ui::UiInstance &instance, const ui::UiCanvas &canvas,
                                     AssetsDb &assets) {
            const std::vector<AssetId> wanted = resolved_stylesheet_ids(canvas, instance);
            if (instance.loaded_sheet_ids == wanted) {
                return;
            }
            if (wanted.empty()) {
                if (instance.stylesheet.has_value() && instance.loaded_sheet_ids.empty()) {
                    instance.loaded_sheet_ids = wanted;
                    return;
                }
                instance.stylesheet.reset();
                instance.loaded_sheet_ids = wanted;
                return;
            }
            ui::Stylesheet merged;
            bool all_found = true;
            for (const AssetId &id: wanted) {
                if (auto sheet = assets.try_get<ui::Stylesheet>(id)) {
                    merged.rules.insert(merged.rules.end(), (*sheet)->rules.begin(), (*sheet)->rules.end());
                    // `@keyframes` of every sheet; a later sheet's block replaces an earlier one of the same name.
                    for (const ui::Keyframes &keyframes: (*sheet)->keyframes) {
                        const auto same = std::find_if(merged.keyframes.begin(), merged.keyframes.end(),
                                                       [&](const ui::Keyframes &kept) {
                                                           return kept.name == keyframes.name;
                                                       });
                        if (same != merged.keyframes.end()) {
                            *same = keyframes;
                        } else {
                            merged.keyframes.push_back(keyframes);
                        }
                    }
                } else {
                    all_found = false;
                    warn_missing_stylesheet(world, id);
                }
            }
            instance.stylesheet = std::move(merged);
            ui::apply_safe_area_environment(canvas, ui::window_size_for(world, canvas.window), *instance.stylesheet);
            if (all_found) {
                instance.loaded_sheet_ids = wanted;
            }
        }

        // Registered only with deps.assets: builds each canvas's UiInstance from its document asset.
        void run_clone_documents(ecs::World &world, AssetsDb &assets) {
            auto view = world.view<ui::UiCanvas>();
            for (ecs::Entity entity: view) {
                ui::UiCanvas &canvas = view.get<ui::UiCanvas>(entity);
                ui::UiInstance *instance = world.try_get<ui::UiInstance>(entity);
                if (instance_needs_rebuild(instance, canvas)) {
                    clone_document(world, entity, canvas, assets, instance);
                }
            }
        }

        // Works on whatever UiInstance a canvas already has, with or without assets.
        void run_bind(ecs::World &world) {
#if defined(ENGINE_UI_PROFILER)
            ui::profiler_attach(world);
#endif
            auto view = world.view<ui::UiCanvas>();
            for (ecs::Entity entity: view) {
                ui::UiCanvas &canvas = view.get<ui::UiCanvas>(entity);
                ui::UiInstance *instance = world.try_get<ui::UiInstance>(entity);
                if (!canvas.data_context || instance == nullptr) {
                    continue;
                }
                ENGINE_UI_PROFILE(entity, Bindings);
                BindErrorLog errors(canvas, *instance);
                (void) ui::apply_bindings(instance->document, *canvas.data_context, &errors, &world.ctx<loc::Catalog>(),
                                          instance->stylesheet ? &*instance->stylesheet : nullptr);
#if defined(ENGINE_UI_PROFILER)
                // Debug and editor builds name a layout-affecting pseudo-class rule in a big tree, once per document.
                if (instance->stylesheet &&
                    (instance->pseudo_checked_generation != instance->stylesheet->generation ||
                     instance->pseudo_checked_builds != instance->document.counters.table_builds)) {
                    instance->pseudo_checked_generation = instance->stylesheet->generation;
                    instance->pseudo_checked_builds = instance->document.counters.table_builds;
                    const std::string name = canvas.document ? std::string(canvas.document->hex()) : "(none)";
                    ui::warn_layout_under_pseudo(instance->document, *instance->stylesheet, name,
                                                 instance->reported_bind_errors,
                                                 [](const std::string &message) { log::warn(message); });
                }
#endif
            }
        }

        // Registered only with deps.assets: merges the stylesheets of every bound canvas.
        void run_stylesheets(ecs::World &world, AssetsDb &assets) {
            auto view = world.view<ui::UiCanvas>();
            for (ecs::Entity entity: view) {
                ui::UiCanvas &canvas = view.get<ui::UiCanvas>(entity);
                ui::UiInstance *instance = world.try_get<ui::UiInstance>(entity);
                if (!canvas.data_context || instance == nullptr) {
                    continue;
                }
                ENGINE_UI_PROFILE(entity, Stylesheets);
                load_merged_stylesheets(world, *instance, canvas, assets);
            }
        }

        // Registered only with deps.assets and deps.audio. Without them sound events are never read.
        void run_audio(ecs::World &world, AssetsDb &assets, IAudioSystem &audio) {
            for (const ecs::Entity entity : world.view<AudioListener>()) {
                const auto &listener = world.get<AudioListener>(entity);
                if (!listener.active) {
                    continue;
                }
                glm::vec3 pos{0.f};
                glm::vec3 forward{0.f, 0.f, -1.f};
                glm::vec3 up{0.f, 1.f, 0.f};
                if (const auto *wt = world.try_get<WorldTransform>(entity)) {
                    pos = glm::vec3(wt->matrix[3]);
                    const glm::vec3 col2 = glm::vec3(wt->matrix[2]);
                    if (glm::length(col2) > 1e-4f) {
                        forward = -glm::normalize(col2);
                    }
                    const glm::vec3 col1 = glm::vec3(wt->matrix[1]);
                    if (glm::length(col1) > 1e-4f) {
                        up = glm::normalize(col1);
                    }
                } else if (const auto *t = world.try_get<Transform>(entity)) {
                    pos = t->position;
                    forward = t->rotation * glm::vec3(0.f, 0.f, -1.f);
                    up = t->rotation * glm::vec3(0.f, 1.f, 0.f);
                }
                audio.set_listener(pos, forward, up, listener.velocity);
                break;
            }

            for (const PlaySfxEvent &event:
                 ecs::EventReader<PlaySfxEvent>{world, world.ctx<ecs::EventCursor<PlaySfxEvent>>()}) {
                auto sound = assets.get<Sound>(event.id);
                audio.play_sfx(*sound, event.volume_scale);
            }

            for (const PlaySpatialSfxEvent &event:
                 ecs::EventReader<PlaySpatialSfxEvent>{world, world.ctx<ecs::EventCursor<PlaySpatialSfxEvent>>()}) {
                auto sound = assets.get<Sound>(event.id);
                audio.play_spatial_sfx(*sound, event.params, event.volume_scale);
            }

            for (const PlayMusicEvent &event:
                 ecs::EventReader<PlayMusicEvent>{world, world.ctx<ecs::EventCursor<PlayMusicEvent>>()}) {
                auto sound = assets.get<Sound>(event.id);
                audio.play_music(*sound, event.loop, event.fade_seconds);
            }

            for (const ecs::Entity entity : world.view<AudioSource3D>()) {
                auto &source = world.get<AudioSource3D>(entity);
                if (!source.playing) {
                    if (source.handle.valid()) {
                        audio.stop_looping_sfx(source.handle);
                        audio.release_looping_sfx(source.handle);
                        source.handle = {};
                    }
                    continue;
                }
                if (const auto *wt = world.try_get<WorldTransform>(entity)) {
                    source.params.position = glm::vec3(wt->matrix[3]);
                } else if (const auto *t = world.try_get<Transform>(entity)) {
                    source.params.position = t->position;
                }
                if (!source.handle.valid()) {
                    source.handle = audio.create_looping_sfx();
                    auto sound = assets.get<Sound>(source.sound);
                    audio.play_looping_sfx(source.handle, *sound);
                }
                audio.set_looping_sfx_spatial(source.handle, source.params);
            }
        }

        // The systems read `*deps.fatal`; an unset one becomes the default exit once, here.
        EngineSystemDeps with_fatal(EngineSystemDeps deps) {
            if (deps.fatal == nullptr) {
                deps.fatal = &default_fatal_error();
            }
            return deps;
        }

        struct SpriteMaterialCache {
            std::unordered_map<const render::ITexture *, std::shared_ptr<render::IMaterial>> materials;
        };

        // Decals do not share the sprite cache: the same texture pointer would otherwise keep whichever RenderState
        // was built first.
        struct DecalMaterialCache {
            std::unordered_map<const render::ITexture *, std::shared_ptr<render::IMaterial>> materials;
        };

        struct DrawItem {
            render::DrawSortKey key;
            render::Command command;
        };

        bool draw_item_less(const DrawItem &a, const DrawItem &b) { return render::draw_sort_less(a.key, b.key); }

        struct WindowClears {
            std::unordered_set<WindowId> ids;
        };

        bool has_command_buffers(const EngineSystemDeps &deps) {
            return deps.commands != nullptr || static_cast<bool>(deps.commands_for_window);
        }

        render::CommandBuffer *command_buffer_for(const EngineSystemDeps &deps, WindowId id) {
            if (deps.commands_for_window) {
                if (render::CommandBuffer *buffer = deps.commands_for_window(id)) {
                    return buffer;
                }
            }
            return id == kPrimaryWindow ? deps.commands : nullptr;
        }

        void clear_window_buffer(ecs::World &world, render::CommandBuffer *buffer, WindowId id) {
            if (buffer == nullptr) {
                return;
            }
            if (world.ctx<WindowClears>().ids.insert(id).second) {
                buffer->clear();
            }
        }

        // A mesh draw is kept when its bounds under `model` intersect any of `frustums`, or when it opted out.
        bool mesh_in_view(const render::IMesh &mesh, const glm::mat4 &model, bool frustum_cull,
                          std::span<const render::Frustum> frustums) {
            return !frustum_cull || render::visible_in_any(frustums, render::transform_aabb(mesh.bounds(), model));
        }

        // Plane is the unrotated XY quad: half |size| on XY, nothing on Z. A spun quad can stick out.
        // Billboard and Stretched use a radius of half the longer side on every axis. That sphere does not
        // cover the quad corner. Mesh is the local box under translate(position) * scale(instance scale),
        // so an off-center box and a negative scale stay correct (`transform_aabb` takes the absolute 3x3).
        render::Aabb particle_world_bounds(const render::ParticleEmitter &emitter, const render::Particle &particle,
                                            const glm::vec3 &position, const render::IMesh &mesh) {
            if (emitter.draw == render::ParticleDraw::Mesh) {
                const float factor =
                        render::particle_mesh_size_factor(emitter.size_curve, particle.age, particle.lifetime);
                const glm::mat4 placed = glm::translate(glm::mat4{1.0f}, position) *
                                         glm::scale(glm::mat4{1.0f}, emitter.mesh_scale * factor);
                return render::transform_aabb(mesh.bounds(), placed);
            }
            const float half_x = std::abs(particle.size.x) * 0.5f;
            const float half_y = std::abs(particle.size.y) * 0.5f;
            if (emitter.draw == render::ParticleDraw::Plane) {
                const glm::vec3 half{half_x, half_y, 0.0f};
                return render::Aabb{.min = position - half, .max = position + half};
            }
            const glm::vec3 extent{std::max(half_x, half_y)};
            return render::Aabb{.min = position - extent, .max = position + extent};
        }

        glm::vec3 particle_draw_position(const render::ParticleEmitter &emitter, const render::Particle &particle,
                                         const glm::mat4 &model) {
            if (emitter.simulation_space == render::SimulationSpace::Local) {
                return glm::vec3(model * glm::vec4(particle.position, 1.0f));
            }
            return particle.position;
        }

        // `frustum_cull` false keeps the emitter. An empty frustum span does too (`visible_in_any`).
        bool emitter_in_view(const render::ParticleEmitter &emitter, const glm::mat4 &model, const render::IMesh &mesh,
                             std::span<const render::Frustum> frustums) {
            if (!emitter.frustum_cull) {
                return true;
            }
            const render::Particle &first = emitter.particles.front();
            const glm::vec3 first_position = particle_draw_position(emitter, first, model);
            render::Aabb box = particle_world_bounds(emitter, first, first_position, mesh);
            for (std::size_t i = 1; i < emitter.particles.size(); ++i) {
                const render::Particle &particle = emitter.particles[i];
                const glm::vec3 position = particle_draw_position(emitter, particle, model);
                const render::Aabb next = particle_world_bounds(emitter, particle, position, mesh);
                box.min = glm::min(box.min, next.min);
                box.max = glm::max(box.max, next.max);
            }
            return render::visible_in_any(frustums, box);
        }

        // One line per process. A null shader is not this case: headless materials still collect.
        void warn_particle_mesh_shader_once() {
            static bool warned = false;
            if (warned) {
                return;
            }
            warned = true;
            log::warn("Particle mesh material shader does not declare instance inputs; skipping the emitter");
        }

        // `frustums`: world space, one per window the draws go to. A renderable, sprite, tilemap chunk, or particle
        // emitter outside all of them is skipped (union: what any window sees is drawn to every window). Empty: no
        // culling. `ParticleEmitter::frustum_cull` false keeps the emitter.
        std::vector<DrawItem> collect_world_draws(ecs::World &world, const glm::mat4 &view, AssetsDb *assets,
                                                  IFatalError *fatal, std::span<const render::Frustum> frustums) {
            auto renderables = world.view<render::Renderable, Transform>();
            auto sprites = world.view<render::Sprite, Transform>();
            auto emitters = world.view<render::ParticleEmitter>();

            std::vector<DrawItem> items;

            for (ecs::Entity entity: renderables) {
                const auto &r = renderables.get<render::Renderable>(entity);
                if (!r.mesh || !r.material) {
                    if (fatal != nullptr) {
                        fatal->report("Renderable is missing mesh or material");
                    }
                    continue;
                }
                const glm::mat4 model = ecs::world_matrix(world, entity);
                if (!mesh_in_view(*r.mesh, model, r.frustum_cull, frustums)) {
                    continue;
                }
                render::CmdDrawMesh cmd;
                cmd.mesh = r.mesh;
                cmd.material = r.material;
                cmd.model = model;
                cmd.color = r.color;
                cmd.uv_scale = {1.0f, 1.0f};
                cmd.uv_offset = {0.0f, 0.0f};
                cmd.material_override = r.material_override;
                const render::SkinDraw *skin = world.try_get<render::SkinDraw>(entity);
                if (skin != nullptr && skin->skin >= 0) {
                    const render::ModelAnimator *animator = world.try_get<render::ModelAnimator>(skin->animator);
                    const bool palette = animator != nullptr && skin->primitive >= 0 &&
                                         static_cast<std::size_t>(skin->primitive) < animator->palettes.size() &&
                                         !animator->palettes[static_cast<std::size_t>(skin->primitive)].empty();
                    if (!palette) {
                        if (fatal != nullptr) {
                            fatal->report("Skinned mesh is missing a bone palette");
                        }
                        continue;
                    }
                    const std::vector<glm::mat4> &bones = animator->palettes[static_cast<std::size_t>(skin->primitive)];
                    cmd.bones = bones.data();
                    cmd.bone_count = static_cast<int>(bones.size());
                }

                const render::DrawSortKey key = render::make_draw_sort_key(
                        r.material.get(), r.layer, r.order_in_layer, glm::vec3(cmd.model[3]), view, entity);
                items.push_back(DrawItem{.key = key, .command = std::move(cmd)});
            }

            std::shared_ptr<render::IMesh> default_quad;
            std::shared_ptr<render::IMesh> default_cube;
            std::shared_ptr<render::IShader> default_shader;
            std::shared_ptr<render::IShader> decal_shader;
            std::shared_ptr<render::IMaterial> default_unlit;
            if (assets != nullptr) {
                if (auto mesh_res = assets->try_get<render::IMesh>(builtin::mesh_quad)) {
                    default_quad = std::move(*mesh_res);
                }
                if (auto cube_res = assets->try_get<render::IMesh>(builtin::mesh_cube)) {
                    default_cube = std::move(*cube_res);
                }
                if (auto shader_res = assets->try_get<render::IShader>(builtin::shader_unlit)) {
                    default_shader = std::move(*shader_res);
                }
                if (auto decal_res = assets->try_get<render::IShader>(builtin::shader_decal)) {
                    decal_shader = std::move(*decal_res);
                }
                if (auto mat_res = assets->try_get<render::IMaterial>(builtin::material_unlit)) {
                    default_unlit = std::move(*mat_res);
                }
            }

            auto &cache = world.ctx<SpriteMaterialCache>();

            for (ecs::Entity entity: sprites) {
                if (world.try_get<render::Renderable>(entity) != nullptr) {
                    continue;
                }
                const auto &s = sprites.get<render::Sprite>(entity);
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
                                default_shader, s.texture, glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                                render::RenderState{.blend = render::BlendMode::Alpha});
                        cache.materials[s.texture.get()] = material;
                    }
                } else {
                    material = default_unlit;
                }

                if (!mesh || !material) {
                    if (fatal != nullptr) {
                        fatal->report("Sprite is missing mesh or material");
                    }
                    continue;
                }
                glm::mat4 model = ecs::world_matrix(world, entity);
                if (s.pixel_size.x > 0.0f && s.pixel_size.y > 0.0f && s.pixels_per_unit > 0.0f) {
                    const glm::vec2 world_size = s.pixel_size / s.pixels_per_unit;
                    const glm::vec3 pivot_offset{(0.5f - s.pivot.x) * world_size.x, (0.5f - s.pivot.y) * world_size.y,
                                                 0.0f};
                    model = glm::translate(model, pivot_offset);
                    model = glm::scale(model, glm::vec3{world_size.x, world_size.y, 1.0f});
                }
                if (s.flip_x || s.flip_y) {
                    model = glm::scale(model, glm::vec3{s.flip_x ? -1.0f : 1.0f, s.flip_y ? -1.0f : 1.0f, 1.0f});
                }
                // The final model carries the pivot, size, and flip, so the quad's box is exactly what is drawn.
                if (!mesh_in_view(*mesh, model, s.frustum_cull, frustums)) {
                    continue;
                }

                render::CmdDrawMesh cmd;
                cmd.mesh = std::move(mesh);
                cmd.material = material;
                cmd.model = model;
                cmd.color = s.color;
                cmd.uv_scale = s.tiling;
                cmd.uv_offset = s.offset;
                cmd.material_override = s.material_override;

                const render::DrawSortKey key = render::make_draw_sort_key(
                        material.get(), s.layer, s.order_in_layer, glm::vec3(cmd.model[3]), view, entity);
                items.push_back(DrawItem{.key = key, .command = std::move(cmd)});
            }

            auto decals = world.view<render::Decal, Transform>();
            for (ecs::Entity entity : decals) {
                const auto &d = decals.get<render::Decal>(entity);
                std::shared_ptr<render::IMesh> mesh = d.mesh ? d.mesh : default_cube;
                std::shared_ptr<render::IMaterial> material;
                if (d.material != nullptr) {
                    material = d.material;
                } else if (d.texture != nullptr && decal_shader != nullptr) {
                    auto &decal_cache = world.ctx<DecalMaterialCache>();
                    auto it = decal_cache.materials.find(d.texture.get());
                    if (it != decal_cache.materials.end()) {
                        material = it->second;
                    } else {
                        material = std::make_shared<render::Material>(
                                decal_shader, d.texture, glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                                render::RenderState{.blend = render::BlendMode::Alpha, .depth_test = false,
                                        .depth_write = false, .cull = render::CullMode::Front});
                        decal_cache.materials[d.texture.get()] = material;
                    }
                }

                if (!mesh || !material) {
                    if (fatal != nullptr) {
                        fatal->report("Decal is missing mesh or material");
                    }
                    continue;
                }

                const glm::mat4 model = ecs::world_matrix(world, entity);
                if (!mesh_in_view(*mesh, model, d.frustum_cull, frustums)) {
                    continue;
                }

                render::CmdDrawDecal cmd;
                cmd.mesh = mesh;
                cmd.material = material;
                cmd.model = model;
                cmd.inv_model = glm::inverse(model);
                cmd.color = d.color;
                cmd.uv_scale = d.uv_scale;
                cmd.uv_offset = d.uv_offset;
                cmd.angle_cutoff = d.angle_cutoff;
                cmd.normal_fade = d.normal_fade;
                cmd.material_override = d.material_override;

                const render::DrawSortKey key = render::make_decal_draw_sort_key(
                        material.get(), d.layer, d.order_in_layer, glm::vec3(model[3]), view, entity);
                items.push_back(DrawItem{.key = key, .command = std::move(cmd)});
            }

            if (assets != nullptr && assets->graphic_factory() != nullptr && default_shader != nullptr) {
                render::IGraphicFactory &factory = *assets->graphic_factory();
                auto tilemaps = world.view<render::Tilemap, Transform>();
                for (ecs::Entity entity: tilemaps) {
                    auto &map = tilemaps.get<render::Tilemap>(entity);
                    if (map.asset == nullptr || map.asset->texture == nullptr) {
                        continue;
                    }
                    const render::ITexture *texture = map.asset->texture.get();
                    std::shared_ptr<render::IMaterial> &material = cache.materials[texture];
                    if (material == nullptr) {
                        material = std::make_shared<render::Material>(
                                default_shader, map.asset->texture, glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                                render::RenderState{.blend = render::BlendMode::Alpha});
                    }
                    const glm::mat4 model = ecs::world_matrix(world, entity);
                    for (render::TilemapChunkDraw &chunk:
                         render::visible_tilemap_chunks(map, factory, model, frustums)) {
                        render::CmdDrawMesh cmd;
                        cmd.mesh = std::move(chunk.mesh);
                        cmd.material = material;
                        cmd.model = model;
                        cmd.color = map.color;
                        cmd.uv_scale = {1.0f, 1.0f};
                        cmd.uv_offset = {0.0f, 0.0f};
                        const render::DrawSortKey key = render::make_draw_sort_key(
                                material.get(), map.layer, map.order_in_layer + chunk.layer_index,
                                glm::vec3(model[3]), view, entity);
                        items.push_back(DrawItem{.key = key, .command = std::move(cmd)});
                    }
                }
            }

            for (ecs::Entity entity: emitters) {
                const auto &em = emitters.get<render::ParticleEmitter>(entity);
                if (em.particles.empty()) {
                    continue;
                }
                // Cards fall back to the builtin quad. Mesh draws that mesh or nothing.
                std::shared_ptr<render::IMesh> mesh =
                        em.draw == render::ParticleDraw::Mesh ? em.mesh : (em.mesh ? em.mesh : default_quad);
                std::shared_ptr<render::IMaterial> material;
                render::BlendMode blend = em.blend;
                if (em.material != nullptr) {
                    material = em.material;
                    blend = em.material->render_state().blend;
                } else if (em.texture != nullptr) {
                    auto it = cache.materials.find(em.texture.get());
                    if (it != cache.materials.end()) {
                        material = it->second;
                    } else if (default_shader != nullptr) {
                        material = std::make_shared<render::Material>(default_shader, em.texture,
                                                                      glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                                                                      render::RenderState{.blend = em.blend});
                        cache.materials[em.texture.get()] = material;
                    }
                } else {
                    material = default_unlit;
                }

                if (!mesh || !material) {
                    if (fatal != nullptr) {
                        fatal->report("ParticleEmitter is missing mesh or material");
                    }
                    continue;
                }
                if (em.draw == render::ParticleDraw::Mesh) {
                    if (const std::shared_ptr<render::IShader> shader = material->shader();
                            shader != nullptr && !shader->declares_particle_instance_inputs()) {
                        warn_particle_mesh_shader_once();
                        continue;
                    }
                }

                glm::mat4 model{1.0f};
                if (em.simulation_space == render::SimulationSpace::Local) {
                    model = ecs::world_matrix(world, entity);
                }
                if (!emitter_in_view(em, model, *mesh, frustums)) {
                    continue;
                }

                render::CmdDrawParticles cmd;
                cmd.mesh = std::move(mesh);
                cmd.material = material;
                cmd.blend = blend;
                cmd.draw = em.draw;

                // Plane is drawn with depth forced off, so it never depth-sorts. Billboard, Stretched, and Mesh
                // follow the material: a transparent depth-sorting batch sorts its own instances back to front.
                const auto view_depth_of = [&view](const glm::vec3 &position) {
                    return -(view * glm::vec4(position, 1.0f)).z;
                };
                const render::RenderState state = material->render_state();
                const bool depth_tested = em.draw != render::ParticleDraw::Plane;
                const bool depth_sorts = depth_tested && state.depth_test;
                const bool sort_instances =
                        depth_sorts && render::draw_queue(state) == render::DrawQueue::Transparent;

                glm::vec3 key_position = glm::vec3(model[3]);
                if (em.draw == render::ParticleDraw::Mesh) {
                    cmd.mesh_instances.reserve(em.particles.size());
                    for (const auto &p: em.particles) {
                        glm::vec3 pos = p.position;
                        if (em.simulation_space == render::SimulationSpace::Local) {
                            pos = glm::vec3(model * glm::vec4(p.position, 1.0f));
                        }
                        const float factor = render::particle_mesh_size_factor(em.size_curve, p.age, p.lifetime);
                        cmd.mesh_instances.push_back(render::particle_mesh_instance(
                                pos, em.mesh_scale, factor, p.color, p.texture_layer));
                    }
                    if (sort_instances) {
                        std::stable_sort(cmd.mesh_instances.begin(), cmd.mesh_instances.end(),
                                [&view_depth_of](const render::MeshParticleInstance &a,
                                        const render::MeshParticleInstance &b) {
                                    return view_depth_of(a.position) > view_depth_of(b.position);
                                });
                    }
                    if (depth_sorts) {
                        const auto farthest = std::max_element(cmd.mesh_instances.begin(), cmd.mesh_instances.end(),
                                [&view_depth_of](const render::MeshParticleInstance &a,
                                        const render::MeshParticleInstance &b) {
                                    return view_depth_of(a.position) < view_depth_of(b.position);
                                });
                        key_position = farthest->position;
                    }
                } else {
                    cmd.instances.reserve(em.particles.size());
                    for (const auto &p: em.particles) {
                        glm::vec3 pos = p.position;
                        glm::vec3 velocity = p.velocity;
                        if (em.simulation_space == render::SimulationSpace::Local) {
                            pos = glm::vec3(model * glm::vec4(p.position, 1.0f));
                            // World-space velocity is already world at spawn. Local velocity is emitter space.
                            velocity = glm::mat3(model) * p.velocity;
                        }
                        cmd.instances.push_back(render::ParticleInstance{
                                .position = pos,
                                .rotation = p.rotation,
                                .size = p.size,
                                .color = p.color,
                                .uv_scale = em.uv_scale,
                                .uv_offset = em.uv_offset,
                                .velocity = velocity,
                        });
                    }
                    if (sort_instances) {
                        std::stable_sort(cmd.instances.begin(), cmd.instances.end(),
                                [&view_depth_of](const render::ParticleInstance &a, const render::ParticleInstance &b) {
                                    return view_depth_of(a.position) > view_depth_of(b.position);
                                });
                    }
                    if (depth_sorts) {
                        const auto farthest = std::max_element(cmd.instances.begin(), cmd.instances.end(),
                                [&view_depth_of](const render::ParticleInstance &a, const render::ParticleInstance &b) {
                                    return view_depth_of(a.position) < view_depth_of(b.position);
                                });
                        key_position = farthest->position;
                    }
                }
                const render::DrawSortKey key = render::make_draw_sort_key(material.get(), em.layer, em.order_in_layer,
                                                                           key_position, view, entity, depth_tested);
                items.push_back(DrawItem{.key = key, .command = std::move(cmd)});
            }

            std::stable_sort(items.begin(), items.end(), draw_item_less);
            return items;
        }

        void run_render(ecs::World &world, const EngineSystemDeps &deps) {
            world.ctx<WindowClears>().ids.clear();
            const std::vector<WindowId> &windows = world.ctx<BoundWindows>().ids;
            for (const WindowId id: windows) {
                clear_window_buffer(world, command_buffer_for(deps, id), id);
            }
            for (const ecs::Entity entity: world.view<ui::UiCanvas>()) {
                const WindowId id = world.get<ui::UiCanvas>(entity).window;
                clear_window_buffer(world, command_buffer_for(deps, id), id);
            }
            if (windows.empty()) {
                return;
            }

            auto renderables = world.view<render::Renderable, Transform>();
            auto sprites = world.view<render::Sprite, Transform>();
            auto emitters = world.view<render::ParticleEmitter>();
            auto tilemaps = world.view<render::Tilemap, Transform>();
            auto decals = world.view<render::Decal, Transform>();
            const bool any_decals = decals.begin() != decals.end();
            const bool any_drawable = renderables.begin() != renderables.end() || sprites.begin() != sprites.end() ||
                                      emitters.begin() != emitters.end() || tilemaps.begin() != tilemaps.end() ||
                                      any_decals;
            const render::PostStack* post_stack = nullptr;
            if (const ActiveCamera* active_camera = world.try_ctx<ActiveCamera>();
                active_camera != nullptr && world.valid(active_camera->entity)) {
                post_stack = world.try_get<render::PostStack>(active_camera->entity);
            }
            auto prepared_post = render::prepare_post_stack(post_stack);
            if (!prepared_post) {
                deps.fatal->report(prepared_post.error());
            }
            const bool post_active = prepared_post->hdr || !prepared_post->effects.empty();
            if (!any_drawable && !post_active) {
                return;
            }

            const ActiveCamera &active = world.ctx<ActiveCamera>();
            if (!world.valid(active.entity)) {
                deps.fatal->report("The scene has renderables but ActiveCamera is not a valid entity");
            }

            const Camera *camera = world.try_get<Camera>(active.entity);
            const Transform *camera_transform = world.try_get<Transform>(active.entity);
            if (camera == nullptr || camera_transform == nullptr) {
                deps.fatal->report("ActiveCamera is missing Camera or Transform");
            }

            const glm::mat4 camera_world = ecs::world_matrix(world, active.entity);
            const glm::mat4 view = view_matrix(camera_world);
            // One frustum per bound window: a draw any of them sees goes to all of them.
            std::vector<render::Frustum> frustums;
            frustums.reserve(windows.size());
            for (const WindowId id: windows) {
                // A minimized or empty window sees nothing, so it keeps nothing visible for the others.
                const ui::WindowSize size = ui::window_size_for(world, id);
                if (size.width > 0 && size.height > 0) {
                    frustums.push_back(camera_frustum(*camera, camera_world, size));
                }
            }
            const std::vector<DrawItem> items = collect_world_draws(world, view, deps.assets, deps.fatal, frustums);
            const render::SceneLights lights =
                    render::collect_scene_lights(world, frustums, glm::vec3(camera_world[3]));
            std::optional<render::Fog> fog;
            if (const render::Fog *camera_fog = world.try_get<render::Fog>(active.entity)) {
                fog = *camera_fog;
            }
            render::PostChain post = render::resolve_post_chain(*prepared_post, deps.assets, deps.fatal);
            if (any_decals) {
                post.sample_depth = true;
                // An otherwise empty chain would leave the scene offscreen. Vulkan presents that copy with the grade
                // shader; OpenGL blits and does not need it.
                if (!post.hdr && post.effects.empty() && post.ldr_copy == nullptr && deps.assets != nullptr) {
                    if (auto copy = deps.assets->try_get<render::IShader>(builtin::shader_post_grade)) {
                        post.ldr_copy = std::move(*copy);
                    }
                }
            }
            const float time = world.try_ctx<Time>() != nullptr ? world.try_ctx<Time>()->total_time : 0.0f;

            for (const WindowId id: windows) {
                render::CommandBuffer *buffer = command_buffer_for(deps, id);
                if (buffer == nullptr) {
                    // A bound window can outlive its buffer for a frame: the OS (or the user) closes it after the
                    // Bind phase synced the dock floats, and Worlds::unbind_window runs on the next sync.
                    continue;
                }
                buffer->push(render::CmdSetView{
                        .view = view,
                        .projection = projection_matrix(*camera, ui::window_size_for(world, id)),
                        .camera_position = glm::vec3(camera_world[3]),
                        .lights = lights,
                        .fog = fog,
                        .time = time,
                        .post = post,
                });
                for (const DrawItem &item: items) {
                    buffer->push(item.command);
                }
                if (post.active()) {
                    buffer->push(render::CmdPostProcess{});
                }
            }
        }

        struct CanvasDraw {
            int order = 0;
            std::uint32_t index = 0;
            render::Rect rect{};
            ui::UiFit fit = ui::UiFit::FillWindow;
            glm::vec2 reference_size{0.0f, 0.0f};
            ui::UiDocument *document = nullptr;
            const ui::Stylesheet *stylesheet = nullptr;
            WindowId window = kPrimaryWindow;
            ecs::Entity entity{};
        };

        void run_ui_render(ecs::World &world, const EngineSystemDeps &deps) {
#if defined(ENGINE_UI_PROFILER)
            ui::profiler_attach(world);
#endif
            ENGINE_UI_PROFILE_SHARED(CommandBuild);

            const Time &time = world.ctx<Time>();
            std::vector<CanvasDraw> canvases;
            {
                auto view = world.view<ui::UiCanvas>();
                for (ecs::Entity entity: view) {
                    ui::UiCanvas &canvas = view.get<ui::UiCanvas>(entity);
                    CanvasDraw draw{canvas.order, entity.index, canvas.rect,   canvas.fit, canvas.reference_size,
                                    nullptr,      nullptr,      canvas.window, entity};
                    if (ui::UiInstance *instance = world.try_get<ui::UiInstance>(entity)) {
                        draw.document = &instance->document;
                        if (instance->stylesheet) {
                            draw.stylesheet = &*instance->stylesheet;
                        }
                    }
                    canvases.push_back(draw);
                }
            }
            std::stable_sort(canvases.begin(), canvases.end(), [](const CanvasDraw &a, const CanvasDraw &b) {
                if (a.order != b.order) {
                    return a.order < b.order;
                }
                return a.index < b.index;
            });
            std::unordered_map<WindowId, std::optional<ecs::Entity>> hover_by_window;
            // A canvas whose open popup is under the pointer owns hover in that window: the canvases it covers
            // must not light up what lies under the popup.
            std::unordered_map<WindowId, std::optional<ecs::Entity>> popup_owner_by_window;
            for (const CanvasDraw &canvas: canvases) {
                if (!popup_owner_by_window.contains(canvas.window)) {
                    popup_owner_by_window.emplace(
                            canvas.window,
                            ui::popup_canvas_at(world, canvas.window, ui::pointer_for(world, canvas.window).position));
                }
            }
            struct PopupLayer {
                render::CommandBuffer *target = nullptr;
                render::CmdDrawUI command;
            };
            std::vector<PopupLayer> popup_layers;
            const bool inspected = ui::inspector_attached(world);
            if (inspected) {
                ui::inspector_retarget(world);
            }
            if (inspected && world.ctx<ui::UiInspector>().pick_pointer) {
                for (const CanvasDraw &canvas: canvases) {
                    if (hover_by_window.contains(canvas.window)) {
                        continue;
                    }
                    hover_by_window.emplace(canvas.window, ui::inspector_hover_canvas(world, canvas.window));
                }
            }
            for (const CanvasDraw &canvas: canvases) {
                const ui::UiPointer &pointer = ui::pointer_for(world, canvas.window);
                const ui::WindowSize size = ui::window_size_for(world, canvas.window);
                const ui::UiCanvasSpace space = ui::canvas_layout_space(canvas.rect, canvas.fit, canvas.reference_size);
                const glm::vec2 media = ui::media_size(space);
                const float window_width = media.x;
                const float window_height = media.y;
                render::CommandBuffer *target = command_buffer_for(deps, canvas.window);
                if (target == nullptr) {
                    // As in run_render: a canvas of a window closed since the last dock sync.
                    continue;
                }
                clear_window_buffer(world, target, canvas.window);

                // Lazily loads into this canvas's window atlas whatever its document/stylesheet actually
                // reference, instead of the old Engine::init behavior of preloading the entire asset
                // catalog up front (see ui/ui_refs.h). builtin::font_ui is ensured unconditionally — it's
                // the fallback for any element with no font-family at all.
                if (canvas.document != nullptr) {
                    if (deps.ensure_ui_font) {
                        deps.ensure_ui_font(canvas.window, builtin::font_ui);
                        for (const AssetId id: ui::collect_referenced_fonts(*canvas.document, canvas.stylesheet)) {
                            deps.ensure_ui_font(canvas.window, id);
                        }
                    }
                    if (deps.ensure_ui_image) {
                        for (const AssetId id: ui::collect_referenced_images(*canvas.document, canvas.stylesheet)) {
                            deps.ensure_ui_image(canvas.window, id);
                        }
                    }
                    if (deps.assets != nullptr) {
                        ui::attach_svg_pictures(*canvas.document, *deps.assets);
                    }
                }

                render::CmdDrawUI cmd{
                        space.layout_rect, canvas.document,
                        canvas.stylesheet, (pointer.position - space.offset) / space.scale,
                        pointer.down,      time.delta_time,
                        window_width,      window_height,
                        space.offset,      space.scale,
                };
                cmd.popup_bounds = ui::popup_bounds(space, size);
                if (const std::optional<ecs::Entity> &owner = popup_owner_by_window[canvas.window];
                    owner.has_value() && *owner != canvas.entity) {
                    constexpr float kNoPointer = -1.0e9f;
                    cmd.pointer = glm::vec2{kNoPointer, kNoPointer};
                    cmd.pointer_down = false;
                }
#if defined(ENGINE_UI_PROFILER)
                if (ui::profiler_recording()) {
                    cmd.canvas = canvas.entity;
                }
#endif
                if (inspected) {
                    if (const auto hover = hover_by_window.find(canvas.window); hover != hover_by_window.end() &&
                                                                                hover->second.has_value() &&
                                                                                *hover->second == canvas.entity) {
                        cmd.inspector_hover = true;
                    }
                    const ui::InspectorPick pick = ui::inspector_selection(world, canvas.window);
                    if (pick.active && pick.canvas == canvas.entity) {
                        cmd.inspector_selection = true;
                        cmd.inspector_selection_path = pick.path;
                        cmd.inspector_selection_owner = pick.generated_owner;
                    }
                }
                if (canvas.document != nullptr && ui::has_open_popup(canvas.document->root)) {
                    popup_layers.push_back(PopupLayer{target, cmd});
                    popup_layers.back().command.popup_layer = true;
                }
                target->push(std::move(cmd));
            }
            // After every base pass, so a popup is drawn above the canvases of higher order too. Same canvas
            // order among the popup layers.
            for (PopupLayer &layer: popup_layers) {
                layer.target->push(std::move(layer.command));
            }
        }

    } // namespace

    void render_world_viewport(ecs::World &scene, const WorldViewportRender &target) {
        if (target.commands == nullptr || target.viewport.w <= 0.0f || target.viewport.h <= 0.0f) {
            return;
        }
        run_model_animations(scene);
        ecs::update_world_transforms(scene);
        run_sprite_animations(scene);

        const glm::mat4 view = view_matrix(target.camera_world);
        const ui::WindowSize viewport_size{
                .width = std::max(1, static_cast<int>(std::lround(target.viewport.w))),
                .height = std::max(1, static_cast<int>(std::lround(target.viewport.h))),
        };
        const render::Frustum frustum = camera_frustum(target.camera, target.camera_world, viewport_size);
        std::vector<DrawItem> items = collect_world_draws(scene, view, target.assets, target.fatal, {&frustum, 1});

        target.commands->push(render::CmdSetView{
                .view = view,
                .projection = projection_matrix(target.camera, viewport_size),
                .camera_position = glm::vec3(target.camera_world[3]),
                .lights = render::collect_scene_lights(scene, std::span<const render::Frustum>(&frustum, 1),
                        glm::vec3(target.camera_world[3])),
                .viewport = target.viewport,
                .clear_color = target.clear_color,
                .time = scene.try_ctx<Time>() != nullptr ? scene.try_ctx<Time>()->total_time : 0.0f,
        });
        for (DrawItem &item: items) {
            target.commands->push(std::move(item.command));
        }
    }

    void run_model_animations(ecs::World &world) {
        const float dt = world.ctx<Time>().delta_time;
        ecs::EventWriter<ecs::ModelAnimEvent> events{world};
        auto view = world.view<render::ModelAnimator>();
        for (ecs::Entity entity: view) {
            auto &animator = view.get<render::ModelAnimator>(entity);
            render::tick_model_animator(animator, dt);
            for (const render::ModelAnimCue &cue: animator.cues) {
                events.send(ecs::ModelAnimEvent{entity, cue.clip, cue.name});
            }
            apply_model_poses(world, animator);
        }
    }

    void run_sprite_animations(ecs::World &world) {
        const float dt = world.ctx<Time>().delta_time;
        auto view = world.view<render::SpriteAnimator, render::Sprite>();
        for (ecs::Entity entity: view) {
            auto &anim = view.get<render::SpriteAnimator>(entity);
            auto &sprite = view.get<render::Sprite>(entity);

            if (!anim.clip || anim.clip->frames.empty()) {
                continue;
            }
            const auto &frames = anim.clip->frames;
            if (!anim.playing || anim.speed <= 0.0f) {
                // Not advancing, but a new, paused, or stopped animator still shows its frame once.
                if (!anim.frame_applied) {
                    sprite.apply_frame(frames[std::min(anim.current_frame, frames.size() - 1)]);
                    anim.frame_applied = true;
                }
                continue;
            }

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

            sprite.apply_frame(frames[anim.current_frame]);
            anim.frame_applied = true;
        }
    }

    namespace {

    // World quaternion from Transform::rotation, parent then child. Scale is not part of it.
    [[nodiscard]] glm::quat particle_collider_rotation(const ecs::World &world, ecs::Entity entity) {
        glm::quat chain[ecs::kMaxHierarchyDepth];
        int count = 0;
        ecs::Entity current = entity;
        for (int step = 0; step < ecs::kMaxHierarchyDepth && world.valid(current); ++step) {
            const Transform *transform = world.try_get<Transform>(current);
            chain[count++] = transform != nullptr ? transform->rotation : glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
            const Parent *parent = world.try_get<Parent>(current);
            if (parent == nullptr || !world.valid(parent->entity)) {
                break;
            }
            current = parent->entity;
        }
        glm::quat world_rotation{1.0f, 0.0f, 0.0f, 0.0f};
        for (int i = count - 1; i >= 0; --i) {
            world_rotation = world_rotation * chain[i];
        }
        return world_rotation;
    }

    // Heading of the matrix X axis in XY, as the 2D solver reads an entity's yaw. Scale changes its length only.
    [[nodiscard]] float particle_collider_yaw(const glm::mat4 &matrix) {
        const float x = matrix[0][0];
        const float y = matrix[0][1];
        if (std::abs(x) < 1.0e-6f && std::abs(y) < 1.0e-6f) {
            return 0.0f;
        }
        return std::atan2(y, x);
    }

    // The nearest RigidBody2D on the entity or above it is Dynamic.
    [[nodiscard]] bool under_dynamic_body_2d(const ecs::World &world, ecs::Entity entity) {
        ecs::Entity current = entity;
        for (int step = 0; step < ecs::kMaxHierarchyDepth && world.valid(current); ++step) {
            if (const RigidBody2D *body = world.try_get<RigidBody2D>(current)) {
                return body->type == BodyType::Dynamic;
            }
            const Parent *parent = world.try_get<Parent>(current);
            if (parent == nullptr || !world.valid(parent->entity)) {
                return false;
            }
            current = parent->entity;
        }
        return false;
    }

    } // namespace

    void run_particles(ecs::World &world, JobSystem *jobs) {
        const float dt = world.ctx<Time>().delta_time;
        // Runs before Phase::Transform, so refresh the matrices that emitters and colliders read (idempotent).
        ecs::update_world_transforms(world);
        auto view = world.view<render::ParticleEmitter>();

        bool any_collisions = false;
        bool any_grid = false;
        for (ecs::Entity entity: view) {
            const render::ParticleEmitter &emitter = view.get<render::ParticleEmitter>(entity);
            if (!emitter.collision_enabled) {
                continue;
            }
            any_collisions = true;
            if (emitter.collision_grid) {
                any_grid = true;
            }
        }

        std::vector<render::ParticleCollider2D> colliders;
        std::vector<render::ParticleCollider3D> colliders_3d;
        if (any_collisions) {
            world.view<Transform, BoxCollider2D>().each(
                    [&](ecs::Entity entity, const Transform &, const BoxCollider2D &box) {
                if (!box.is_trigger) {
                    colliders.push_back(render::ParticleCollider2D{
                            .shape = render::ParticleCollider2D::Shape::Box,
                            .position = ecs::world_position(world, entity),
                            .box_size = box.size,
                            .circle_radius = 0.0f,
                            .layer = box.layer,
                    });
                }
            });
            world.view<Transform, CircleCollider2D>().each(
                    [&](ecs::Entity entity, const Transform &, const CircleCollider2D &circle) {
                if (!circle.is_trigger) {
                    colliders.push_back(render::ParticleCollider2D{
                            .shape = render::ParticleCollider2D::Shape::Circle,
                            .position = ecs::world_position(world, entity),
                            .box_size = glm::vec3{0.0f},
                            .circle_radius = circle.radius,
                            .layer = circle.layer,
                    });
                }
            });
            // The 2D solver ignores an SdfCollider2D under a Dynamic body, and so do particles. `field` points into
            // the pool for this call only: no entity is created or destroyed before the emitters below are stepped.
            world.view<Transform, SdfCollider2D>().each(
                    [&](ecs::Entity entity, const Transform &, const SdfCollider2D &sdf) {
                if (under_dynamic_body_2d(world, entity)) {
                    return;
                }
                const glm::mat4 matrix = ecs::world_matrix(world, entity);
                colliders.push_back(render::ParticleCollider2D{
                        .shape = render::ParticleCollider2D::Shape::Sdf,
                        .position = glm::vec3(matrix[3]),
                        .box_size = glm::vec3{0.0f},
                        .circle_radius = 0.0f,
                        .layer = sdf.layer,
                        .field = &sdf.field,
                        .angle = particle_collider_yaw(matrix),
                });
            });
            world.view<Transform, BoxCollider3D>().each(
                    [&](ecs::Entity entity, const Transform &, const BoxCollider3D &box) {
                if (!box.is_trigger) {
                    colliders_3d.push_back(render::ParticleCollider3D{
                            .shape = render::ParticleCollider3D::Shape::Box,
                            .position = ecs::world_position(world, entity),
                            .rotation = particle_collider_rotation(world, entity),
                            .box_size = box.size,
                            .radius = 0.0f,
                            .height = 0.0f,
                            .layer = box.layer,
                    });
                }
            });
            world.view<Transform, SphereCollider3D>().each(
                    [&](ecs::Entity entity, const Transform &, const SphereCollider3D &sphere) {
                if (!sphere.is_trigger) {
                    colliders_3d.push_back(render::ParticleCollider3D{
                            .shape = render::ParticleCollider3D::Shape::Sphere,
                            .position = ecs::world_position(world, entity),
                            .rotation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
                            .box_size = glm::vec3{0.0f},
                            .radius = sphere.radius,
                            .height = 0.0f,
                            .layer = sphere.layer,
                    });
                }
            });
            world.view<Transform, CapsuleCollider3D>().each(
                    [&](ecs::Entity entity, const Transform &, const CapsuleCollider3D &capsule) {
                if (!capsule.is_trigger) {
                    colliders_3d.push_back(render::ParticleCollider3D{
                            .shape = render::ParticleCollider3D::Shape::Capsule,
                            .position = ecs::world_position(world, entity),
                            .rotation = particle_collider_rotation(world, entity),
                            .box_size = glm::vec3{0.0f},
                            .radius = capsule.radius,
                            .height = capsule.height,
                            .layer = capsule.layer,
                    });
                }
            });
        }

        const render::ParticleGrid *grid = nullptr;
        if (any_grid) {
            grid = world.try_ctx<render::ParticleGrid>();
            if (grid != nullptr && grid->is_solid == nullptr) {
                grid = nullptr;
            }
        }

        for (ecs::Entity entity: view) {
            auto &emitter = view.get<render::ParticleEmitter>(entity);
            const render::ParticleGrid *emitter_grid =
                    (emitter.collision_enabled && emitter.collision_grid) ? grid : nullptr;
            render::update_emitter(
                    emitter, dt, ecs::world_matrix(world, entity), colliders, colliders_3d, jobs, emitter_grid);
        }
    }

    void register_simulation_systems(ecs::World &world, EngineSystemDeps deps) {
        deps = with_fatal(std::move(deps));
        if (world.ctx<SimulationSystemsRegistered>().value) {
            return;
        }
        world.ctx<SimulationSystemsRegistered>().value = true;
        world.add_system(ecs::Schedule::Fixed, ecs::Phase::Physics,
                         [jobs = deps.jobs](ecs::World &w) { run_physics_2d(w, jobs); });
        world.add_system(ecs::Schedule::Fixed, ecs::Phase::Physics,
                         [jobs = deps.jobs](ecs::World &w) { run_physics_3d(w, jobs); });
        nav::register_nav_systems(world);
        world.add_system(ecs::Schedule::Frame, ecs::Phase::Game, [](ecs::World &w) { run_model_animations(w); });
        world.add_system(ecs::Schedule::Frame, ecs::Phase::Game, [](ecs::World &w) { run_sprite_animations(w); });
        world.add_system(ecs::Schedule::Frame, ecs::Phase::Game, [](ecs::World &w) { animation::run_tweens(w); });
        world.add_system(ecs::Schedule::Frame, ecs::Phase::Game,
                         [jobs = deps.jobs](ecs::World &w) { run_particles(w, jobs); });
        world.add_system(ecs::Schedule::Frame, ecs::Phase::Transform,
                         [](ecs::World &w) { ecs::update_world_transforms(w); });
        // Without a command buffer there is nothing to render into.
        if (has_command_buffers(deps)) {
            world.add_system(ecs::Schedule::Frame, ecs::Phase::Render, [deps](ecs::World &w) { run_render(w, deps); });
        }
    }

    void register_ui_systems(ecs::World &world, EngineSystemDeps deps) {
        deps = with_fatal(std::move(deps));
        if (world.ctx<UiSystemsRegistered>().value) {
            return;
        }
        world.ctx<UiSystemsRegistered>().value = true;
        world.add_system(ecs::Schedule::Frame, ecs::Phase::Input, [](ecs::World &w) { run_input(w); });
        // After run_input: a press on dock chrome is judged against the canvases the user saw.
        world.add_system(ecs::Schedule::Frame, ecs::Phase::Input, [deps](ecs::World &w) { ui::run_dock_input(w, deps); });
        // After the UI, always: a target only sees the pointer the UI did not take, and only then adds its window to
        // the click-through set. The order is the registration order, so no caller can get it wrong.
        world.add_system(ecs::Schedule::Frame, ecs::Phase::Input, [](ecs::World &w) { run_pointer_targets(w); });
        world.add_system(ecs::Schedule::Frame, ecs::Phase::Input, [](ecs::World &w) { run_ui_windows(w); });
        world.add_system(ecs::Schedule::Frame, ecs::Phase::Input, [](ecs::World &w) { run_splash_timers(w); });
        // Before run_bind: panel and chrome canvases follow the layout the Game phase left.
        world.add_system(ecs::Schedule::Frame, ecs::Phase::Bind, [deps](ecs::World &w) { ui::run_dock_layout(w, deps); });
        // Without assets no document is cloned and no stylesheet merged: a canvas keeps the UiInstance it was given.
        if (deps.assets != nullptr) {
            AssetsDb &assets = *deps.assets;
            world.add_system(ecs::Schedule::Frame, ecs::Phase::Bind,
                             [&assets](ecs::World &w) { run_clone_documents(w, assets); });
        }
        world.add_system(ecs::Schedule::Frame, ecs::Phase::Bind, [](ecs::World &w) { run_bind(w); });
        if (deps.assets != nullptr) {
            AssetsDb &assets = *deps.assets;
            world.add_system(ecs::Schedule::Frame, ecs::Phase::Bind,
                             [&assets](ecs::World &w) { run_stylesheets(w, assets); });
        }
        // Without a command buffer there is nothing to draw into.
        if (has_command_buffers(deps)) {
            world.add_system(ecs::Schedule::Frame, ecs::Phase::UiRender,
                             [deps](ecs::World &w) { run_ui_render(w, deps); });
        }
    }

    void register_audio_systems(ecs::World &world, EngineSystemDeps deps) {
        deps = with_fatal(std::move(deps));
        if (world.ctx<AudioSystemsRegistered>().value) {
            return;
        }
        world.ctx<AudioSystemsRegistered>().value = true;
        // Without assets and audio the world has no audio system: PlaySfxEvent and PlayMusicEvent are never read.
        if (deps.assets != nullptr && deps.audio != nullptr) {
            AssetsDb &assets = *deps.assets;
            IAudioSystem &audio = *deps.audio;
            world.add_system(ecs::Schedule::Frame, ecs::Phase::Audio,
                             [&assets, &audio](ecs::World &w) { run_audio(w, assets, audio); });
        }
    }

    void register_engine_systems(ecs::World &world, EngineSystemDeps deps) {
        world.ctx<EngineSystemsRegistered>().value = true;
        register_simulation_systems(world, deps);
        register_ui_systems(world, deps);
        register_audio_systems(world, deps);
    }

} // namespace engine
