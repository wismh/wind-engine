# File index

Every first-party file under `include/engine/`, `src/`, `tests/`, `tools/`, and `editor/`, and the CMake scripts directly in `cmake/`. `external/`, `build/`, and `cmake-build-*` are not listed. There is no per-file page. The link is the module or feature that owns the file.

## `include/engine/`

| Path | What it does | Page |
| --- | --- | --- |
| `include/engine/engine.h` | Umbrella include | [Core](../modules/Core.md) |
| `include/engine/game_entry.h` | `ENGINE_GAME(GameClass)`: `main` or the module exports; the module's CRT guard | [Core](../modules/Core.md) |
| `include/engine/igame.h` | `IGame`, `GameBase`, `SplashScreen` | [Core](../modules/Core.md) |
| `include/engine/log.h` | `log::init`, `info`, `warn`, `error` | [Core](../modules/Core.md) |
| `include/engine/builtin_ids.h` | Frozen builtin `AssetId` values | [Resources](../modules/Resources.md) |
| `include/engine/audio/.gitkeep` | Keeps the directory in Git | [Audio](../modules/Audio.md) |
| `include/engine/audio/audio_system.h` | `IAudioSystem`, `AudioSystem`, pool sizes | [Audio](../modules/Audio.md) |
| `include/engine/audio/events.h` | `PlaySfxEvent`, `PlayMusicEvent` | [Audio](../modules/Audio.md) |
| `include/engine/audio/sound.h` | `Sound` and the opaque `Audio` clip | [Audio](../modules/Audio.md) |
| `include/engine/core/app_lifecycle.h` | Pause, resume, terminate | [Core](../modules/Core.md) |
| `include/engine/core/application_state.h` | `running` and `paused` | [Core](../modules/Core.md) |
| `include/engine/core/bound_windows.h` | Windows bound to one world | [Windowing](../features/Windowing.md) |
| `include/engine/core/build_info.h` | `build_id()` and the generated `kBuildId` | [CMake](../build/CMake.md) |
| `include/engine/core/cli_commands.h` | `CliCommand`, `CliReply`, `CliCommands`: a host's own `wind-cli` commands | [CLI](../features/CLI.md#host-commands) |
| `include/engine/core/engine.h` | `Engine<GameT>::init`, `run`, `dispose` over `EngineHost` | [Core](../modules/Core.md) |
| `include/engine/core/engine_host.h` | `EngineHost`: services, primary window, catalogs, game attach and detach, run | [Core](../modules/Core.md) |
| `include/engine/core/engine_runtime.h` | Windowed presentation and `GameLoop` owner | [Core](../modules/Core.md) |
| `include/engine/core/engine_services.h` | References passed into the game constructor | [Core](../modules/Core.md) |
| `include/engine/core/export.h` | `ENGINE_API` export and import macro | [CMake](../build/CMake.md) |
| `include/engine/core/fixed_step.h` | `FixedStepClock` | [Core](../modules/Core.md) |
| `include/engine/core/file_dialog.h` | `FileFilter`, `FileDialogResult`, `FileDialogCall`: one owned dialog, `take`, `cancel` | [Windowing](../features/Windowing.md) |
| `include/engine/core/game_module.h` | Game module export types and symbol names; `GameModule`, `load_game_module`, `purge_game_module_copies` | [Core](../modules/Core.md) |
| `include/engine/core/host.h` | Headless tick host for tests | [Core](../modules/Core.md) |
| `include/engine/core/input_system.h` | `ActionId` bindings and input events | [Input Mapper](../features/Input%20Mapper.md) |
| `include/engine/core/key_code.h` | `KeyCode` values matching SDL scancodes | [Input Mapper](../features/Input%20Mapper.md) |
| `include/engine/core/platform.h` | Platform, assets root, `user_data_directory` | [Core](../modules/Core.md) |
| `include/engine/core/run_hooks.h` | `RunHooks`: `on_start`, `on_frame_end`, `on_quit`, `cli` | [Runtime Loop](../architecture/Runtime%20Loop.md) |
| `include/engine/core/sdl_fatal_error.h` | SDL message-box `IFatalError` | [Core](../modules/Core.md) |
| `include/engine/core/time.h` | `Time` and the 60 Hz constants | [Core](../modules/Core.md) |
| `include/engine/core/web_loop.h` | `MainLoopPolicy` and `LoopShutdown` | [Core](../modules/Core.md) |
| `include/engine/core/window_control.h` | `IWindowControl` (including `open_windows`, `set_title`, `request_open_file`) and `OverlayMode` | [Windowing](../features/Windowing.md) |
| `include/engine/core/window_desc.h` | `WindowId`, `WindowDesc`, `WindowStyle` | [Windowing](../features/Windowing.md) |
| `include/engine/core/worlds.h` | Process `Worlds`: add, destroy, bind, stepping | [Core](../modules/Core.md) |
| `include/engine/ecs/.gitkeep` | Keeps the directory in Git | [ECS](../modules/ECS.md) |
| `include/engine/ecs/camera.h` | Orthographic `Camera` and screen/world conversion | [ECS](../modules/ECS.md) |
| `include/engine/ecs/entity.h` | Generational `Entity` | [ECS](../modules/ECS.md) |
| `include/engine/ecs/events.h` | Double-buffered `Events`, reader, writer, cursor | [ECS](../modules/ECS.md) |
| `include/engine/ecs/physics.h` | Colliders, `CollisionEvent`, `run_physics` | [ECS](../modules/ECS.md) |
| `include/engine/ecs/schedule.h` | `Schedule` and `Phase` | [ECS](../modules/ECS.md) |
| `include/engine/ecs/systems.h` | `register_engine_systems` and its dependencies | [ECS](../modules/ECS.md) |
| `include/engine/ecs/transform.h` | Position, rotation, scale. No parent | [ECS](../modules/ECS.md) |
| `include/engine/ecs/world.h` | `World` and `View` declarations | [ECS](../modules/ECS.md) |
| `include/engine/ecs/world.inl` | Pool and view template bodies | [ECS](../modules/ECS.md) |
| `include/engine/haptics/haptics_system.h` | `IHaptics` and `HapticsSystem` | [Haptics](../modules/Haptics.md) |
| `include/engine/net/http_call.h` | `HttpCall`: one owned request, `take`, `cancel` | [Net](../modules/Net.md) |
| `include/engine/net/http_client.h` | `IHttpClient` and `HttpClient` | [Net](../modules/Net.md) |
| `include/engine/net/http_request.h` | `HttpRequest`, `HttpResponse`, `HttpError`, `HttpResult` | [Net](../modules/Net.md) |
| `include/engine/process/process_call.h` | `ProcessCall`: one owned child, `take_output`, `take`, `cancel` | [Process](../modules/Process.md) |
| `include/engine/process/process_desc.h` | `ProcessDesc`, `ProcessVariable`, `ProcessExit`, `ProcessError`, `ProcessResult` | [Process](../modules/Process.md) |
| `include/engine/process/process_launcher.h` | `IProcessLauncher` and `ProcessLauncher` | [Process](../modules/Process.md) |
| `include/engine/project/manifest_error.h` | `ManifestError`, `ManifestFailure`, `describe` | [Project](../modules/Project.md) |
| `include/engine/project/sdk_manifest.h` | `SdkManifest`, `read_sdk_manifest` | [Project](../modules/Project.md) |
| `include/engine/project/wind_project.h` | `WindProject`, `read_wind_project` | [Project](../modules/Project.md) |
| `include/engine/loc/catalog.h` | `StringTable` parse and `Catalog` | [Localization](../modules/Localization.md) |
| `include/engine/render/.gitkeep` | Keeps the directory in Git | [Render](../modules/Render.md) |
| `include/engine/render/animation.h` | Sprite clip, animator, animation TOML | [Render](../modules/Render.md) |
| `include/engine/render/backend.h` | `IRenderBackend::execute` | [Render](../modules/Render.md) |
| `include/engine/render/canvas.h` | `ICanvas::draw` | [Render](../modules/Render.md) |
| `include/engine/render/command_buffer.h` | Buffer of `Command` | [Render](../modules/Render.md) |
| `include/engine/render/commands.h` | `CmdDrawMesh`, `CmdDrawUI`, `CmdDrawParticles` | [Render](../modules/Render.md) |
| `include/engine/render/curve.h` | `Curve<T>` with `Linear`, `Smooth`, and `Step` keys | [Render](../modules/Render.md) |
| `include/engine/render/graphic_factory.h` | Mesh, shader, and texture descriptions and factory | [Render](../modules/Render.md) |
| `include/engine/render/graphics.h` | `IMesh`, `IShader`, `ITexture` | [Render](../modules/Render.md) |
| `include/engine/render/material.h` | `IMaterial`, blend modes, `.mat` parse | [Materials and Sort](../features/Materials%20and%20Sort.md) |
| `include/engine/render/particles.h` | `ParticleEmitter` | [Render](../modules/Render.md) |
| `include/engine/render/renderable.h` | `Renderable` and the sort predicate | [Materials and Sort](../features/Materials%20and%20Sort.md) |
| `include/engine/render/shader_adapt.h` | GLSL 330 to GLSL 300 ES | [Render](../modules/Render.md) |
| `include/engine/render/sprite.h` | `Sprite` quad fields | [Materials and Sort](../features/Materials%20and%20Sort.md) |
| `include/engine/resources/.gitkeep` | Keeps the directory in Git | [Resources](../modules/Resources.md) |
| `include/engine/resources/asset_guid.h` | `write_missing_metas` | [Asset Codegen](../build/Asset%20Codegen.md) |
| `include/engine/resources/asset_id.h` | 32-hex `AssetId` | [Resources](../modules/Resources.md) |
| `include/engine/resources/assets_db.h` | `get`, `try_get`, `get_sprite` | [Assets](../features/Assets.md) |
| `include/engine/resources/fatal_error.h` | `IFatalError` | [Core](../modules/Core.md) |
| `include/engine/resources/font.h` | `Font` file bytes | [Resources](../modules/Resources.md) |
| `include/engine/resources/meta.h` | Importers, `.meta` parse, `codegen_write` | [Resources](../modules/Resources.md) |
| `include/engine/resources/sprite_sheet.h` | Named rect to sprite UVs | [Assets](../features/Assets.md) |
| `include/engine/ui/.gitkeep` | Keeps the directory in Git | [UI](../modules/UI.md) |
| `include/engine/ui/bindable.h` | `Bindable` and `BindableList` | [UI](../modules/UI.md) |
| `include/engine/ui/binding_id.h` | FNV-1a `BindingId` | [UI](../modules/UI.md) |
| `include/engine/ui/builder.h` | `ui::Node` document builder | [UI Markup](../features/UI%20Markup.md) |
| `include/engine/ui/canvas.h` | `UiCanvas`, pointer, focus, `MouseConsumed` | [UI](../modules/UI.md) |
| `include/engine/ui/command.h` | `ICommand` and `RelayCommand` | [UI](../modules/UI.md) |
| `include/engine/ui/dock_geometry.h` | Dock rects, chrome and drop hit tests, splitter and float resize helpers | [Docking](../features/Docking.md) |
| `include/engine/ui/dock_layout.h` | `DockLayout`: tab stacks, splits, floats, operations, text, reconcile | [Docking](../features/Docking.md) |
| `include/engine/ui/dock_space.h` | `DockSpace` component, `DockFloatMode`, `DockPanel`, `DockPanelCloseRequested`, order count, close button rect | [Docking](../features/Docking.md#host) |
| `include/engine/ui/document.h` | `Element`, layout boxes, `UiDocument` | [UI Markup](../features/UI%20Markup.md) |
| `include/engine/ui/draw_list.h` | `IDrawList` for `IPaint` | [UI](../modules/UI.md) |
| `include/engine/ui/inspector.h` | Inspector probe: attach, pick state, tree, select, toggle, detail, rules | [UI Inspector](../features/UI%20Inspector.md) |
| `include/engine/ui/paint.h` | `IPaint` and `RelayPaint` | [UI](../modules/UI.md) |
| `include/engine/ui/presentation.h` | Process window sizes, pointer, mouse consumption | [Windowing](../features/Windowing.md) |
| `include/engine/ui/profiler.h` | Profiler probe: attach, canvases, frames, Pause. No-ops without the macro | [UI Profiler](../features/UI%20Profiler.md) |
| `include/engine/ui/splash.h` | `show_splash` and `SplashTimer` | [UI](../modules/UI.md) |
| `include/engine/ui/stylesheet.h` | Parsed CSS rules | [UI Markup](../features/UI%20Markup.md) |
| `include/engine/ui/tree.h` | `flatten_tree`, `TreeExpansion`, `tree_navigate`: a tree as flat rows | [UI](../modules/UI.md#trees) |
| `include/engine/ui/text_line.h` | Wrapped rows and painted selection boxes | [UI Input](../features/UI%20Input.md) |
| `include/engine/ui/view_model.h` | Property, command, and paint registration | [UI](../modules/UI.md) |

## `src/core/` and `src/cli/`

| Path | What it does | Page |
| --- | --- | --- |
| `src/core/app_lifecycle.cpp` | Applies lifecycle events to `ApplicationState` | [Core](../modules/Core.md) |
| `src/core/back_key_filter.h`, `src/core/back_key_filter.cpp` | `BackKeyFilter`: Android back delivered to `InputSystem` or used to dismiss text input | [Input Mapper](../features/Input%20Mapper.md) |
| `src/core/build_info.cpp` | `build_id()` returns the engine's `kBuildId` | [CMake](../build/CMake.md) |
| `src/core/call_completions.h` | `CallCompletions`: async call results from any thread, handed over on the main thread | [Windowing](../features/Windowing.md) |
| `src/core/engine_host.cpp` | `EngineHost` body | [Core](../modules/Core.md) |
| `src/core/engine_instantiate.cpp` | Explicit `Engine<WindowSmokeGame>` instantiation | [Core](../modules/Core.md) |
| `src/core/engine_runtime.cpp` | `EngineRuntime` pimpl over the presentation | [Core](../modules/Core.md) |
| `src/core/file_dialog_call.cpp` | `FileDialogCall` ownership and cancel | [Windowing](../features/Windowing.md) |
| `src/core/file_dialog_state.h` | State shared by a call and the dialog callback, `FileDialogCompletions` | [Windowing](../features/Windowing.md) |
| `src/core/fixed_step.cpp` | Accumulator and step cap | [Core](../modules/Core.md) |
| `src/core/frame_capture.h` | `FrameCapture`: one window's pixels, read before its swap | [CLI](../features/CLI.md#screenshot) |
| `src/core/frame_limiter.h`, `src/core/frame_limiter.cpp` | `FrameLimiter`: sleep schedule for frames no vsync swap waits on | [Windowing](../features/Windowing.md#frame-pacing) |
| `src/core/event_window.h`, `src/core/event_window.cpp` | `event_window`: the window an SDL event goes to; a closed window's id is dropped | [Windowing](../features/Windowing.md#events-of-a-window) |
| `src/core/frame_pacing.h`, `src/core/frame_pacing.cpp` | `choose_vsync_window`, `frame_period`, `limiter_period` | [Windowing](../features/Windowing.md#frame-pacing) |
| `src/core/frame_step.cpp` | `flush_worlds` and `simulate_worlds` | [Runtime Loop](../architecture/Runtime%20Loop.md) |
| `src/core/frame_step.h` | Declarations for those two functions | [Runtime Loop](../architecture/Runtime%20Loop.md) |
| `src/core/game_module.cpp` | Copy, load, check, and unload a game module (window builds) | [Core](../modules/Core.md) |
| `src/core/game_loop.cpp` | `begin`, `tick`, `reentrant_tick`, `end`, `RunHooks` calls | [Runtime Loop](../architecture/Runtime%20Loop.md) |
| `src/core/game_loop.h` | `GameLoop` | [Runtime Loop](../architecture/Runtime%20Loop.md) |
| `src/core/host.cpp` | Registers systems and ticks a fake canvas | [Core](../modules/Core.md) |
| `src/core/input_system.cpp` | Bind table and event enqueue | [Input Mapper](../features/Input%20Mapper.md) |
| `src/core/log.cpp` | spdlog sinks | [Core](../modules/Core.md) |
| `src/core/platform.cpp` | Assets root, Android staging, user-data path | [Core](../modules/Core.md) |
| `src/core/presentation.h` | `IPresentation` seam the loop calls | [Core](../modules/Core.md) |
| `src/core/sdl_fatal_error.cpp` | Message box and quit | [Core](../modules/Core.md) |
| `src/core/web_loop.cpp` | RAF vs blocking policy, ordered shutdown | [Core](../modules/Core.md) |
| `src/core/worlds.cpp` | Add, destroy, bind, per-world clocks | [Core](../modules/Core.md) |
| `src/cli/cli_commands.cpp` | JSON for `tree`, `element`, `hit`, `click`, `profile`, and host replies; `element_window_rect` | [CLI](../features/CLI.md) |
| `src/cli/json.h` | `Json`, the streaming writer of CLI bodies | [CLI](../features/CLI.md) |
| `src/cli/screenshot.cpp`, `src/cli/screenshot.h` | `screenshot`: snap, crop, PNG, reply | [CLI](../features/CLI.md#screenshot) |
| `src/cli/cli_server.cpp` | Loopback accept thread and descriptor file | [CLI](../features/CLI.md) |
| `src/cli/cli_server.h` | Server API used by `GameLoop` | [CLI](../features/CLI.md) |

## `src/ecs/`

| Path | What it does | Page |
| --- | --- | --- |
| `src/ecs/.gitkeep` | Keeps the directory in Git | [ECS](../modules/ECS.md) |
| `src/ecs/camera.cpp` | View and projection matrices, screen/world | [ECS](../modules/ECS.md) |
| `src/ecs/physics.cpp` | Velocity integration and overlap events | [ECS](../modules/ECS.md) |
| `src/ecs/systems.cpp` | Engine systems: input, dock input, splash, animation, particles, dock layout, bind, audio, render, UI render | [Runtime Loop](../architecture/Runtime%20Loop.md) |
| `src/ecs/world.cpp` | Create, destroy, flush, `run` | [ECS](../modules/ECS.md) |

## `src/resources/`

| Path | What it does | Page |
| --- | --- | --- |
| `src/resources/.gitkeep` | Keeps the directory in Git | [Resources](../modules/Resources.md) |
| `src/resources/asset_guid.cpp` | Writes missing `.meta` sidecars | [Asset Codegen](../build/Asset%20Codegen.md) |
| `src/resources/assets_db.cpp` | Catalog load and typed asset cache | [Assets](../features/Assets.md) |
| `src/resources/codegen.cpp` | Scan tree, emit `asset_ids.h` and `catalog.toml` | [Asset Codegen](../build/Asset%20Codegen.md) |
| `src/resources/icon_codegen.cpp` | Resize and encode ico, icns, mipmaps, favicon | [Icon Codegen](../build/Icon%20Codegen.md) |
| `src/resources/icon_codegen.h` | Icon encode API used by the host tool | [Icon Codegen](../build/Icon%20Codegen.md) |
| `src/resources/importers.cpp` | Mesh text, shader XML, PNG decode entry points | [Assets](../features/Assets.md) |
| `src/resources/importers.h` | Those parsers, and `encode_png_rgba` | [Assets](../features/Assets.md) |
| `src/resources/meta.cpp` | TOML `.meta` and cooked catalog parse | [Resources](../modules/Resources.md) |
| `src/resources/png_decode.cpp` | PNG bytes to RGBA `TextureDesc` | [Icon Codegen](../build/Icon%20Codegen.md) |
| `src/resources/png_encode.cpp` | RGBA `TextureDesc` to PNG bytes (`encode_png_rgba`) | [Icon Codegen](../build/Icon%20Codegen.md) |
| `src/resources/stb_image.h` | Vendored PNG/image decoder | [Resources](../modules/Resources.md) |
| `src/resources/stb_image_resize2.h` | Vendored resampler used by icon codegen | [Icon Codegen](../build/Icon%20Codegen.md) |
| `src/resources/stb_image_write.h` | Vendored PNG encoder behind `encode_png_rgba` | [Icon Codegen](../build/Icon%20Codegen.md) |
| `src/resources/stb_truetype.h` | Vendored TrueType rasterizer used by the UI painter | [UI](../modules/UI.md) |

## `src/render/`

| Path | What it does | Page |
| --- | --- | --- |
| `src/render/.gitkeep` | Keeps the directory in Git | [Render](../modules/Render.md) |
| `src/render/backend/opengl/.gitkeep` | Empty placeholder. The backend lives in `src/render/opengl/` | [Render](../modules/Render.md) |
| `src/render/animation.cpp` | Parse animation TOML into a clip | [Render](../modules/Render.md) |
| `src/render/framebuffer_image.cpp`, `src/render/framebuffer_image.h` | Read-back rows to a top-first image with straight alpha | [CLI](../features/CLI.md#screenshot) |
| `src/render/material.cpp` | Parse `.mat` TOML | [Materials and Sort](../features/Materials%20and%20Sort.md) |
| `src/render/material_instance.h` | `IMaterial` for a loaded `.mat` | [Materials and Sort](../features/Materials%20and%20Sort.md) |
| `src/render/particles.cpp` | Spawn, integrate, and collide particles | [Render](../modules/Render.md) |
| `src/render/shader_adapt.cpp` | Rewrite GLSL for ES | [Render](../modules/Render.md) |
| `src/render/opengl/clipboard.cpp` | SDL clipboard get and set | [UI Input](../features/UI%20Input.md) |
| `src/render/opengl/clipboard.h` | Those two functions | [UI Input](../features/UI%20Input.md) |
| `src/render/opengl/desktop_overlay_policy.cpp` | Click-through and the Win32 modal-loop hook | [Windowing](../features/Windowing.md) |
| `src/render/opengl/desktop_overlay_policy.h` | `DesktopOverlayPolicy` | [Windowing](../features/Windowing.md) |
| `src/render/opengl/gl_includes.h` | glad or GLES include, private | [Render](../modules/Render.md) |
| `src/render/opengl/nanovg_painter.cpp` | `IUiPainter` on NanoVG | [UI](../modules/UI.md) |
| `src/render/opengl/nanovg_painter.h` | Painter declaration | [UI](../modules/UI.md) |
| `src/render/opengl/opengl_backend.cpp` | Execute mesh and particle commands | [Render](../modules/Render.md) |
| `src/render/opengl/opengl_backend.h` | `OpenGLBackend` | [Render](../modules/Render.md) |
| `src/render/opengl/opengl_canvas.cpp` | Per-window GL canvas: render, read back, present; font and image upload, swap interval | [Render](../modules/Render.md) |
| `src/render/opengl/opengl_canvas.h` | `OpenGLCanvas` | [Render](../modules/Render.md) |
| `src/render/opengl/opengl_factory.cpp` | Create GL mesh, shader, texture | [Render](../modules/Render.md) |
| `src/render/opengl/opengl_factory.h` | `OpenGLFactory` | [Render](../modules/Render.md) |
| `src/render/opengl/opengl_mesh.cpp` | GL mesh | [Render](../modules/Render.md) |
| `src/render/opengl/opengl_mesh.h` | GL mesh type | [Render](../modules/Render.md) |
| `src/render/opengl/opengl_shader.cpp` | GL program and named uniforms | [Render](../modules/Render.md) |
| `src/render/opengl/opengl_shader.h` | GL shader type | [Render](../modules/Render.md) |
| `src/render/opengl/opengl_texture.cpp` | GL texture and sampler from catalog filter and wrap | [Render](../modules/Render.md) |
| `src/render/opengl/opengl_texture.h` | GL texture type | [Render](../modules/Render.md) |
| `src/render/opengl/sdl_gl_presentation.cpp` | SDL poll, present, and `IPresentation` | [Windowing](../features/Windowing.md) |
| `src/render/opengl/sdl_gl_presentation.h` | `SdlGlPresentation` | [Windowing](../features/Windowing.md) |
| `src/render/opengl/window_control.h` | `IWindowControl` adapter over `WindowManager` | [Windowing](../features/Windowing.md) |
| `src/render/opengl/window_manager.cpp` | One window, canvas, and command buffer per id; frame pacing in `draw_all` | [Windowing](../features/Windowing.md) |
| `src/render/opengl/window_manager.h` | `WindowManager` | [Windowing](../features/Windowing.md) |
| `src/render/opengl/window_system.cpp` | One SDL window and GL context, including `set_icon` | [Windowing](../features/Windowing.md) |
| `src/render/opengl/window_system.h` | `WindowSystem` and `make_icon_surface` | [Windowing](../features/Windowing.md) |

## `src/ui/`

| Path | What it does | Page |
| --- | --- | --- |
| `src/ui/.gitkeep` | Keeps the directory in Git | [UI](../modules/UI.md) |
| `src/ui/bind_scan.h` | Collect `{binding}` paths for codegen | [Asset Codegen](../build/Asset%20Codegen.md) |
| `src/ui/builder.cpp` | `ui::Node` factories | [UI Markup](../features/UI%20Markup.md) |
| `src/ui/canvas.cpp` | Fit, hit routing, focus, text edit, splash timers' frame hook | [UI](../modules/UI.md) |
| `src/ui/css_length.h` | Parse `px`, `%`, `em`, and `calc()` | [UI Markup](../features/UI%20Markup.md) |
| `src/ui/css_parser.cpp` | Stylesheet parser | [UI Markup](../features/UI%20Markup.md) |
| `src/ui/dock_chrome.cpp`, `dock_chrome.h` | Chrome document (an ItemsControl per box kind), its view-models, the tab probe | [Docking](../features/Docking.md#host) |
| `src/ui/dock_float_windows.cpp` | OS float windows: open as tool windows of the space's window, follow native moves, apply rects and titles, close (also when the world goes); re-dock on the close button | [Docking](../features/Docking.md#os-window-floats) |
| `src/ui/dock_geometry.cpp` | Stack, splitter, and float rects; split, resize, and clamp helpers | [Docking](../features/Docking.md) |
| `src/ui/dock_hit.cpp` | Chrome hit test and drop targets | [Docking](../features/Docking.md) |
| `src/ui/dock_layout.cpp` | Dock tree, operations, invariants | [Docking](../features/Docking.md) |
| `src/ui/dock_reconcile.cpp` | Drop unknown panels, add missing ones | [Docking](../features/Docking.md) |
| `src/ui/dock_runtime.h` | Private dock state: chrome canvases, float windows, gesture, cursors, measured tab widths; the two dock systems; tab measuring, metrics, float home, per-window geometry, and order helpers | [Docking](../features/Docking.md#host) |
| `src/ui/dock_space_input.cpp` | `run_dock_input`: chrome presses, gestures across windows, Escape, float window close, `MouseConsumed` | [Docking](../features/Docking.md#host) |
| `src/ui/dock_space_layout.cpp` | `run_dock_layout`: panel and chrome canvases from the geometry of each window, float home, orders | [Docking](../features/Docking.md#host) |
| `src/ui/dock_tab_measure.cpp` | Tab widths from the titles, the window's painter, and the theme's `.dock-tab` | [Docking](../features/Docking.md#tab-width) |
| `src/ui/dock_text.cpp` | Layout to and from TOML | [Docking](../features/Docking.md) |
| `src/ui/document.cpp` | Bind, layout, hit-test, virtualization | [UI Markup](../features/UI%20Markup.md) |
| `src/ui/draw_list_adapter.h` | `IDrawList` over `IUiPainter` | [UI](../modules/UI.md) |
| `src/ui/element_path.cpp` | Resolve a child-index path, including generated rows | [UI Inspector](../features/UI%20Inspector.md) |
| `src/ui/element_path.h` | `kGeneratedPathBit` and path helpers | [UI Inspector](../features/UI%20Inspector.md) |
| `src/ui/inline_math.cpp` | Split `\(...\)` out of label text | [UI](../modules/UI.md) |
| `src/ui/inline_math.h` | Inline-math split API | [UI](../modules/UI.md) |
| `src/ui/input_batch.h` | Per-`run_input` bind and layout cache | [UI Input](../features/UI%20Input.md) |
| `src/ui/inspector.cpp` | Inspector probe: tree walk, detail text, matched rules, retarget | [UI Inspector](../features/UI%20Inspector.md) |
| `src/ui/paint.cpp` | Cascade, paint, style cache | [UI Markup](../features/UI%20Markup.md) |
| `src/ui/painter.h` | Private `IUiPainter` | [UI](../modules/UI.md) |
| `src/ui/profile.h` | `ENGINE_UI_PROFILE` scopes | [UI Profiler](../features/UI%20Profiler.md) |
| `src/ui/profiler.cpp` | Profiler rings, recording world, snapshots, CLI JSON | [UI Profiler](../features/UI%20Profiler.md) |
| `src/ui/splash.cpp` | Build the two splash documents | [UI](../modules/UI.md) |
| `src/ui/splash.h` | Splash document structs used by `show_splash` | [UI](../modules/UI.md) |
| `src/ui/style_anim.cpp` | Sample `transition` and `@keyframes` | [UI Markup](../features/UI%20Markup.md) |
| `src/ui/style_anim.h` | Motion clock types | [UI Markup](../features/UI%20Markup.md) |
| `src/ui/stylesheet.cpp` | `next_stylesheet_generation` counter, one per engine | [UI Markup](../features/UI%20Markup.md) |
| `src/ui/text_select.cpp` | Word ranges and selection edits | [UI Input](../features/UI%20Input.md) |
| `src/ui/text_select.h` | Selection helpers | [UI Input](../features/UI%20Input.md) |
| `src/ui/text_wrap.cpp` | Break a string into rows | [UI](../modules/UI.md) |
| `src/ui/text_wrap.h` | Wrap API | [UI](../modules/UI.md) |
| `src/ui/tr_attr.cpp` | Parse `{tr …}` attribute text | [Localization](../modules/Localization.md) |
| `src/ui/tr_attr.h` | `{tr}` parse result | [Localization](../modules/Localization.md) |
| `src/ui/ui_refs.cpp` | Images and fonts a document can paint | [UI](../modules/UI.md) |
| `src/ui/ui_refs.h` | Those collectors | [UI](../modules/UI.md) |
| `src/ui/view_model.cpp` | Non-template `ViewModel` methods | [UI](../modules/UI.md) |
| `src/ui/xml_parser.cpp` | XML to `Element` | [UI Markup](../features/UI%20Markup.md) |
| `src/ui/math/math_ast.h` | Formula AST nodes | [UI](../modules/UI.md) |
| `src/ui/math/math_element.cpp` | `Math` element measure and paint | [UI](../modules/UI.md) |
| `src/ui/math/math_element.h` | Math element entry used by the document | [UI](../modules/UI.md) |
| `src/ui/math/math_font.cpp` | OpenType MATH table reads | [UI](../modules/UI.md) |
| `src/ui/math/math_font.h` | Math font metrics | [UI](../modules/UI.md) |
| `src/ui/math/math_layout.cpp` | Box layout for a formula | [UI](../modules/UI.md) |
| `src/ui/math/math_layout.h` | Layout API | [UI](../modules/UI.md) |
| `src/ui/math/math_paint.cpp` | Outline strokes for a laid-out formula | [UI](../modules/UI.md) |
| `src/ui/math/math_paint.h` | Paint API | [UI](../modules/UI.md) |
| `src/ui/math/math_parser.cpp` | TeX subset parser | [UI](../modules/UI.md) |
| `src/ui/math/math_parser.h` | Parser API | [UI](../modules/UI.md) |
| `src/ui/math/math_stretch.cpp` | Glyph stretch for tall delimiters | [UI](../modules/UI.md) |
| `src/ui/math/math_stretch.h` | Stretch API | [UI](../modules/UI.md) |

## `src/audio/`, `src/haptics/`, `src/net/`, `src/loc/`

| Path | What it does | Page |
| --- | --- | --- |
| `src/audio/audio_system.cpp` | Real mixer or fake mixer behind `ENGINE_WITH_AUDIO` | [Audio](../modules/Audio.md) |
| `src/audio/clip.cpp` | Decode a WAV into the opaque clip | [Audio](../modules/Audio.md) |
| `src/audio/clip.h` | Clip storage. Mixer types stay here | [Audio](../modules/Audio.md) |
| `src/audio/fake_mixer.h` | In-memory tracks for tests and the no-audio build | [Audio](../modules/Audio.md) |
| `src/haptics/fake_haptics.h` | Records vibrate and cancel on every backend | [Haptics](../modules/Haptics.md) |
| `src/haptics/haptics_system.cpp` | No-op, `navigator.vibrate`, or Android JNI | [Haptics](../modules/Haptics.md) |
| `src/net/android_http.cpp` | `AndroidHttp`: `HttpURLConnection` through JNI | [Net](../modules/Net.md) |
| `src/net/android_http.h` | Android backend API | [Net](../modules/Net.md) |
| `src/net/http_call.cpp` | `HttpCall` ownership and cancel | [Net](../modules/Net.md) |
| `src/net/http_call_state.h` | State shared by a call, the queue, and the backend | [Net](../modules/Net.md) |
| `src/net/http_client.cpp` | `HttpClient`: backend choice, `send`, `poll`, `dispose` | [Net](../modules/Net.md) |
| `src/net/http_completions.h` | `HttpCompletions`, the `CallCompletions` of HTTP calls | [Net](../modules/Net.md) |
| `src/net/http_parse.cpp` | URL check, method names, raw headers, `HttpResponse::header` | [Net](../modules/Net.md) |
| `src/net/http_parse.h` | `HttpUrl` and the parse functions | [Net](../modules/Net.md) |
| `src/net/http_worker_pool.cpp` | Threads for blocking backends | [Net](../modules/Net.md) |
| `src/net/http_worker_pool.h` | Worker pool API | [Net](../modules/Net.md) |
| `src/net/web_fetch.cpp` | `emscripten_fetch` backend | [Net](../modules/Net.md) |
| `src/net/web_fetch.h` | Web backend API | [Net](../modules/Net.md) |
| `src/net/winhttp_session.cpp` | `WinHttpSession`: WinHTTP backend | [Net](../modules/Net.md) |
| `src/net/winhttp_session.h` | Windows backend API | [Net](../modules/Net.md) |
| `src/process/line_splitter.cpp` | `LineSplitter`: output bytes to lines | [Process](../modules/Process.md) |
| `src/process/line_splitter.h` | `LineSplitter` API | [Process](../modules/Process.md) |
| `src/process/process_call.cpp` | `ProcessCall` ownership and cancel | [Process](../modules/Process.md) |
| `src/process/process_call_state.h` | State shared by a call, the launcher, and the reader thread | [Process](../modules/Process.md) |
| `src/process/process_command_line.cpp` | Windows argument quoting, command line, environment merge | [Process](../modules/Process.md) |
| `src/process/process_command_line.h` | Their API | [Process](../modules/Process.md) |
| `src/process/process_launcher.cpp` | `ProcessLauncher`: backend choice, `run`, `launch`, `poll`, `dispose` | [Process](../modules/Process.md) |
| `src/process/windows_process.cpp` | Windows backend: pipe, job object, reader thread, `launch` | [Process](../modules/Process.md) |
| `src/process/windows_process.h` | Windows backend API | [Process](../modules/Process.md) |
| `src/project/manifest_error.cpp` | `to_string`, `describe` | [Project](../modules/Project.md) |
| `src/project/manifest_table.cpp` | Loads a manifest with toml++, typed keys | [Project](../modules/Project.md) |
| `src/project/manifest_table.h` | `ManifestTable` | [Project](../modules/Project.md) |
| `src/project/sdk_manifest.cpp` | `read_sdk_manifest` | [Project](../modules/Project.md) |
| `src/project/wind_project.cpp` | `read_wind_project` | [Project](../modules/Project.md) |
| `src/loc/catalog.cpp` | Table parse, lookup, warn-once, pseudo | [Localization](../modules/Localization.md) |
| `src/loc/format.cpp` | `{name}` and plural message format | [Localization](../modules/Localization.md) |
| `src/loc/format.h` | Format API | [Localization](../modules/Localization.md) |
| `src/loc/plural.cpp` | Integer plural categories | [Localization](../modules/Localization.md) |
| `src/loc/plural.h` | `PluralCategory` | [Localization](../modules/Localization.md) |

## `tests/`

| Path | What it does | Page |
| --- | --- | --- |
| `tests/android_assets_test.cpp` | Android asset staging without a device | [Runtime Assets](../build/Runtime%20Assets.md) |
| `tests/android_lifecycle_test.cpp` | Pause, resume, back key routing | [Core](../modules/Core.md) |
| `tests/animation_test.cpp` | Sprite clip parse and playback | [Render](../modules/Render.md) |
| `tests/assets_test.cpp` | Catalog, `get` / `try_get`, `unload_catalog`, codegen failures | [Assets](../features/Assets.md) |
| `tests/audio_test.cpp` | Pool, music fade, looping handles, `stop_all` without opening a device | [Audio](../modules/Audio.md) |
| `tests/builtin_test.cpp` | Frozen builtin ids and files | [Resources](../modules/Resources.md) |
| `tests/camera_test.cpp` | Ortho matrices and screen/world | [ECS](../modules/ECS.md) |
| `tests/cli_server_test.cpp` | Descriptor, HTTP, window routing, host commands, and UI commands without `GameLoop` | [CLI](../features/CLI.md) |
| `tests/cmake_sanity_test.cpp` | Public headers compile, build id, CMake file checks (game functions, SDK package) | [CMake](../build/CMake.md) |
| `tests/command_buffer_test.cpp` | Push and iterate the three command types | [Render](../modules/Render.md) |
| `tests/dock_layout_test.cpp` | Dock operations and invariants, geometry, drop zones, chrome hits, text round trip, reconcile | [Docking](../features/Docking.md) |
| `tests/dock_space_test.cpp` | Dock systems: panel rects and orders, chrome lists and default theme, every tab and splitter hit, tab click and drag, splitter, float move, resize, raise, dock, Escape, consumption, close request, tab widths from titles | [Docking](../features/Docking.md#host) |
| `tests/dock_float_window_test.cpp` | OS window floats with the fake window control: open, bind, title, rect, mode switch, tear-out, drags across windows, native move and resize, close button, space removed, world destroyed, space window closed, owner and utility style, `dock_panel_os_window`, open failure, saved rect, off-display float | [Docking](../features/Docking.md#os-window-floats) |
| `tests/ecs_test.cpp` | Create, destroy, views, deferred destroy | [ECS](../modules/ECS.md) |
| `tests/events_test.cpp` | Double buffer and cursors | [ECS](../modules/ECS.md) |
| `tests/file_dialog_test.cpp` | `FileDialogCall` delivery, cancel, and ownership | [Windowing](../features/Windowing.md) |
| `tests/fixtures/cli_client.h` | Loopback client: this process's descriptor and `POST /exec` | [CLI](../features/CLI.md) |
| `tests/fixtures/fake_services.h` | Headless `EngineServices` fakes for tests that create a game; the window control keeps each window's description, position, size, and title | [Core](../modules/Core.md) |
| `tests/fixtures/game_module/fixture_game.cpp` | Fixture game module, built three ways in the editor build | [Core](../modules/Core.md) |
| `tests/fixtures/game_module/fixture_log.h` | `FixtureLog` the fixture game writes into | [Core](../modules/Core.md) |
| `tests/event_window_test.cpp` | `event_window`: a live window, a closed window's id dropped, id 0 per event kind | [Windowing](../features/Windowing.md#events-of-a-window) |
| `tests/frame_pacing_test.cpp` | Vsync window choice, refresh period, limiter period, `FrameLimiter` schedule | [Windowing](../features/Windowing.md#frame-pacing) |
| `tests/game_entry_test.cpp` | `ENGINE_GAME` module exports and build id (window builds) | [Core](../modules/Core.md) |
| `tests/game_module_test.cpp` | `load_game_module` against the fixture modules (editor build) | [Core](../modules/Core.md) |
| `tests/game_loop_test.cpp` | `RunHooks` order, frame-end restart, and `RunHooks::cli` without a primary world, with a fake presentation | [Runtime Loop](../architecture/Runtime%20Loop.md) |
| `tests/haptics_test.cpp` | Clamp, no-op, and the fake counters | [Haptics](../modules/Haptics.md) |
| `tests/http_test.cpp` | `HttpCall` ownership, completion queue, worker pool, URL and header parsing, `HttpClient` without a socket | [Net](../modules/Net.md) |
| `tests/host_test.cpp` | `Host` tick and system registration | [Core](../modules/Core.md) |
| `tests/icon_codegen_test.cpp` | ICO, ICNS, and PNG sizes in memory | [Icon Codegen](../build/Icon%20Codegen.md) |
| `tests/input_test.cpp` | Bind, hold, touch synthesis, `reset` | [Input Mapper](../features/Input%20Mapper.md) |
| `tests/loc_catalog_test.cpp` | TOML tables, fallback, pseudo, warn-once | [Localization](../modules/Localization.md) |
| `tests/loc_format_test.cpp` | Placeholders and plural branches | [Localization](../modules/Localization.md) |
| `tests/log_test.cpp` | The null sink | [Core](../modules/Core.md) |
| `tests/material_test.cpp` | `.mat` parse and instance tint | [Materials and Sort](../features/Materials%20and%20Sort.md) |
| `tests/mvvm_test.cpp` | View-model properties, commands, checkbox write-back | [UI](../modules/UI.md) |
| `tests/opengl_texture_test.cpp` | CPU texture description. No GL draw | [Render](../modules/Render.md) |
| `tests/particle_test.cpp` | Emitter step and curves | [Render](../modules/Render.md) |
| `tests/physics_test.cpp` | Overlap enter, stay, exit | [ECS](../modules/ECS.md) |
| `tests/platform_test.cpp` | Assets root and `user_data_directory` names | [Core](../modules/Core.md) |
| `tests/process_test.cpp` | Line cutting, quoting, environment, `ProcessCall` ownership, and real `cmd.exe` children on Windows (output, exit, cancel, leftovers) | [Process](../modules/Process.md) |
| `tests/project_test.cpp` | `wind_project.toml` and `sdk.toml` reading and their errors | [Project](../modules/Project.md) |
| `tests/render_system_test.cpp` | `run_render` sort; unset camera skips, missing components fatal | [Materials and Sort](../features/Materials%20and%20Sort.md) |
| `tests/shader_adapt_test.cpp` | GLSL 300 ES rewrite | [Render](../modules/Render.md) |
| `tests/sort_test.cpp` | `renderable_less` | [Materials and Sort](../features/Materials%20and%20Sort.md) |
| `tests/splash_test.cpp` | Splash documents and the timer | [UI](../modules/UI.md) |
| `tests/sprite_test.cpp` | Sprite sheet UVs and `get_sprite` | [Assets](../features/Assets.md) |
| `tests/time_test.cpp` | Clamp, pause, and the step cap | [Core](../modules/Core.md) |
| `tests/ui_builder_test.cpp` | `ui::Node` tree matches XML | [UI Markup](../features/UI%20Markup.md) |
| `tests/ui_css_test.cpp` | Selectors, lengths, unknown properties, stylesheet generation | [UI Markup](../features/UI%20Markup.md) |
| `tests/ui_display_none_test.cpp` | `display: none` skips layout and hit-test | [UI Markup](../features/UI%20Markup.md) |
| `tests/ui_inline_math_test.cpp` | `\(...\)` splits inside a label | [UI](../modules/UI.md) |
| `tests/ui_input_batch_test.cpp` | One bind per canvas inside `run_input` | [UI Input](../features/UI%20Input.md) |
| `tests/ui_inspector_test.cpp` | Pick, paths, tree, select, toggle, detail, rules, overlay | [UI Inspector](../features/UI%20Inspector.md) |
| `tests/ui_items_control_virtualization_test.cpp` | Row window and spacers | [UI](../modules/UI.md) |
| `tests/ui_tree_test.cpp` | Tree rows, expansion, keys, `var()` indent, collapse in a virtualized list, scroll into view | [UI](../modules/UI.md#trees) |
| `tests/ui_label_select_test.cpp` | Label selection and copy | [UI Input](../features/UI%20Input.md) |
| `tests/ui_layout_dirty_gate_test.cpp` | Skip layout when the gate is clean | [UI](../modules/UI.md) |
| `tests/ui_layout_hit_test.cpp` | Stack layout and hit order | [UI Input](../features/UI%20Input.md) |
| `tests/ui_loc_test.cpp` | `{tr}` bind and locale switch | [Localization](../modules/Localization.md) |
| `tests/ui_math_element_test.cpp` | `Math` element in a document | [UI](../modules/UI.md) |
| `tests/ui_math_font_test.cpp` | MATH table metrics | [UI](../modules/UI.md) |
| `tests/ui_math_layout_test.cpp` | Formula box layout | [UI](../modules/UI.md) |
| `tests/ui_math_paint_test.cpp` | Formula outlines | [UI](../modules/UI.md) |
| `tests/ui_math_parser_test.cpp` | TeX subset parse | [UI](../modules/UI.md) |
| `tests/ui_math_stretch_test.cpp` | Stretched delimiters | [UI](../modules/UI.md) |
| `tests/ui_paint_binding_test.cpp` | `IPaint` draw order | [UI](../modules/UI.md) |
| `tests/ui_painter_test.cpp` | Recording painter, no GL | [UI](../modules/UI.md) |
| `tests/ui_profiler_test.cpp` | Profiler scopes, world isolation, rings, snapshots, and the no-op build | [UI Profiler](../features/UI%20Profiler.md) |
| `tests/ui_refs_test.cpp` | Referenced images and fonts | [UI](../modules/UI.md) |
| `tests/ui_scroll_test.cpp` | Scroll view and wheel | [UI Input](../features/UI%20Input.md) |
| `tests/ui_text_input_test.cpp` | Caret, clipboard, IME | [UI Input](../features/UI%20Input.md) |
| `tests/ui_text_wrap_test.cpp` | Row breaks and line height | [UI](../modules/UI.md) |
| `tests/ui_xml_test.cpp` | Tags, bindings, unknown elements | [UI Markup](../features/UI%20Markup.md) |
| `tests/web_loop_test.cpp` | RAF policy and shutdown order | [Core](../modules/Core.md) |
| `tests/window_icon_test.cpp` | `make_icon_surface` byte layout | [Windowing](../features/Windowing.md) |
| `tests/window_style_test.cpp` | Style flags (`utility` included), overlay mode, window control without a window, `raise` of no window, a primary window with an owner refused | [Windowing](../features/Windowing.md) |
| `tests/worlds_test.cpp` | World isolation, window routing, per-window draw, registration order | [Core](../modules/Core.md) |

## `tools/`

| Path | What it does | Page |
| --- | --- | --- |
| `templates/empty/**` | The empty project template the SDK installs and the launcher copies | [Launcher](../features/Launcher.md#template) |
| `tools/asset_codegen/main.cpp` | `asset_codegen` CLI | [Asset Codegen](../build/Asset%20Codegen.md) |
| `tools/asset_codegen/README.md` | One-page usage | [Asset Codegen](../build/Asset%20Codegen.md) |
| `tools/asset_guid/main.cpp` | `asset_guid` CLI | [Asset Codegen](../build/Asset%20Codegen.md) |
| `tools/asset_guid/README.md` | One-page usage | [Asset Codegen](../build/Asset%20Codegen.md) |
| `tools/icon_codegen/main.cpp` | `icon_codegen` CLI | [Icon Codegen](../build/Icon%20Codegen.md) |
| `tools/wind_cli/main.cpp` | Host client for the loopback server, and `launch` of the editor beside it | [CLI](../features/CLI.md) |

## `cmake/`

| Path | What it does | Page |
| --- | --- | --- |
| `cmake/build_id.cmake` | Writes `<engine/build_id.h>`: the build id and the engine's CRT macros | [CMake](../build/CMake.md#build-id) |
| `cmake/wind_game.cmake` | `engine_add_game`, `engine_configure_app`, `engine_prepare_runtime`, web and Android variants, `engine_sdk_configurations` (SDK mode: `DebugGame` and `Release`) | [CMake](../build/CMake.md#game-functions) |
| `cmake/WindConfig.cmake.in` | Template of the editor SDK's `WindConfig.cmake` (`find_package(Wind)`): imported `engine`, `glm::glm`, tools, GoogleTest | [CMake](../build/CMake.md#editor-sdk) |
| `cmake/sdk_manifest.cmake` | Install step that writes the SDK's `sdk.toml` (version, commit, dirty, config, build id) | [CMake](../build/CMake.md#editor-sdk) |

## `editor/`

Built only with `ENGINE_EDITOR`. See [Editor](../features/Editor.md).

| Path | What it does | Page |
| --- | --- | --- |
| `editor/CMakeLists.txt` | `wind_editor` and `wind_editor_tests` | [CMake](../build/CMake.md) |
| `editor/assets/css/editor.css` | Editor window style | [Editor](../features/Editor.md) |
| `editor/assets/css/editor.css.meta` | Its GUID sidecar | [Assets](../features/Assets.md) |
| `editor/assets/css/panels.css` | Project, UI Tree, Inspector, Profiler, and Build panel style | [Editor](../features/Editor.md) |
| `editor/assets/css/panels.css.meta` | Its GUID sidecar | [Assets](../features/Assets.md) |
| `editor/assets/ui/build.xml` | Build tab: summary and log | [Editor](../features/Editor.md) |
| `editor/assets/ui/build.xml.meta` | Its GUID sidecar | [Assets](../features/Assets.md) |
| `editor/assets/ui/editor.xml` | Editor window: the toolbar; the panels' dock space is below it | [Editor](../features/Editor.md) |
| `editor/assets/ui/editor.xml.meta` | Its GUID sidecar | [Assets](../features/Assets.md) |
| `editor/assets/ui/inspector.xml` | Inspector tab: title, subtitle, sections that collapse | [Editor](../features/Editor.md#inspector) |
| `editor/assets/ui/inspector.xml.meta` | Its GUID sidecar | [Assets](../features/Assets.md) |
| `editor/assets/ui/profiler.xml` | Profiler tab: Pause, canvases, charts, numbers | [UI Profiler](../features/UI%20Profiler.md) |
| `editor/assets/ui/profiler.xml.meta` | Its GUID sidecar | [Assets](../features/Assets.md) |
| `editor/assets/ui/ui_tree.xml` | UI Tree tab: Pick, element tree | [UI Inspector](../features/UI%20Inspector.md) |
| `editor/assets/ui/ui_tree.xml.meta` | Its GUID sidecar | [Assets](../features/Assets.md) |
| `editor/src/asset_inspection.cpp` | Folder, File, Import, and Content sections of a project file | [Editor](../features/Editor.md#inspector) |
| `editor/src/asset_inspection.h` | `inspect_asset`, the Content limits | [Editor](../features/Editor.md#inspector) |
| `editor/src/asset_selection.h` | `AssetSelection` | [Editor](../features/Editor.md#inspector) |
| `editor/src/build_line_view_model.cpp` | Binds a log row | [Editor](../features/Editor.md) |
| `editor/src/build_line_view_model.h` | `BuildLineViewModel` | [Editor](../features/Editor.md) |
| `editor/src/build_panel.cpp` | Log lines, tones, first error, line cap | [Editor](../features/Editor.md) |
| `editor/src/build_panel.h` | `BuildPanel`, `LineTone`, `tone_of` | [Editor](../features/Editor.md) |
| `editor/src/build_view_model.cpp` | Binds the view-model to `build.xml` and its scroll | [Editor](../features/Editor.md) |
| `editor/src/build_view_model.h` | `BuildViewModel` | [Editor](../features/Editor.md) |
| `editor/src/dock_layout_file.cpp` | Read and atomically write the panel layout file | [Editor](../features/Editor.md#layout-file) |
| `editor/src/dock_layout_file.h` | `DockLayoutFile` | [Editor](../features/Editor.md#layout-file) |
| `editor/src/editor_app.cpp` | Start, SDK and project, build and play, frame-end transitions, quit | [Editor](../features/Editor.md) |
| `editor/src/editor_app.h` | `EditorApp` | [Editor](../features/Editor.md) |
| `editor/src/editor_cli.cpp` | `state`, `play`, `stop`, `open` for `wind-cli` | [Editor](../features/Editor.md#wind-cli) |
| `editor/src/editor_cli.h` | `EditorCli`, `EditorFacts` | [Editor](../features/Editor.md#wind-cli) |
| `editor/src/editor_options.cpp` | `--project` and `--play` | [Editor](../features/Editor.md) |
| `editor/src/editor_options.h` | `EditorOptions` | [Editor](../features/Editor.md) |
| `editor/src/editor_panels.cpp` | Panel canvases in a `DockSpace` with OS window floats, default layout, show, tree keys, visible-only refresh, layout saves, attach and detach | [Editor](../features/Editor.md#panels) |
| `editor/src/editor_panels.h` | `EditorPanels` | [Editor](../features/Editor.md) |
| `editor/src/editor_selection.cpp` | Select and clear, the revision | [Editor](../features/Editor.md#inspector) |
| `editor/src/editor_selection.h` | `EditorSelection`, `SelectionTarget` | [Editor](../features/Editor.md#inspector) |
| `editor/src/editor_view_model.cpp` | Binds the view-model to `editor.xml` | [Editor](../features/Editor.md) |
| `editor/src/editor_view_model.h` | `EditorViewModel` | [Editor](../features/Editor.md) |
| `editor/src/engine_host_play.cpp` | `IPlayHost` over `EngineHost` and `EditorPanels` | [Editor](../features/Editor.md) |
| `editor/src/engine_host_play.h` | `EngineHostPlay` | [Editor](../features/Editor.md) |
| `editor/src/inspector_line_view_model.cpp` | Binds a section line | [Editor](../features/Editor.md#inspector) |
| `editor/src/inspector_line_view_model.h` | `InspectorLineViewModel` | [Editor](../features/Editor.md#inspector) |
| `editor/src/inspector_panel.cpp` | Shows the editor's selection: a file read on select, a UI element every refresh | [Editor](../features/Editor.md#inspector) |
| `editor/src/inspector_panel.h` | `InspectorPanel` | [Editor](../features/Editor.md#inspector) |
| `editor/src/inspector_section.h` | `InspectorSection`: a heading and its lines | [Editor](../features/Editor.md#inspector) |
| `editor/src/inspector_section_view_model.cpp` | Section heading, chevron, and toggle method | [Editor](../features/Editor.md#inspector) |
| `editor/src/inspector_section_view_model.h` | `InspectorSectionViewModel` | [Editor](../features/Editor.md#inspector) |
| `editor/src/inspector_view_model.cpp` | Binds the view-model to `inspector.xml` | [Editor](../features/Editor.md#inspector) |
| `editor/src/inspector_view_model.h` | `InspectorViewModel` | [Editor](../features/Editor.md#inspector) |
| `editor/src/main.cpp` | `wind_editor` entry | [Editor](../features/Editor.md) |
| `editor/src/method_command.h` | `MethodCommand`: an `ICommand` bound to one method | [Editor](../features/Editor.md) |
| `editor/src/play_host.h` | `IPlayHost`: the window, catalog, and panel half of Play and Stop | [Editor](../features/Editor.md) |
| `editor/src/play_session.cpp` | Play and the Stop order | [Editor](../features/Editor.md) |
| `editor/src/play_session.h` | `PlaySession` | [Editor](../features/Editor.md) |
| `editor/src/profiler_chart.h` | Stacked chart geometry | [UI Profiler](../features/UI%20Profiler.md) |
| `editor/src/profiler_chart_paint.cpp` | Draws the chart through `IDrawList` | [UI Profiler](../features/UI%20Profiler.md) |
| `editor/src/profiler_chart_paint.h` | `ProfilerChartPaint` | [UI Profiler](../features/UI%20Profiler.md) |
| `editor/src/profiler_panel.cpp` | Copies the profiler rings into the view-model; the numbers | [UI Profiler](../features/UI%20Profiler.md) |
| `editor/src/profiler_panel.h` | `ProfilerPanel` | [UI Profiler](../features/UI%20Profiler.md) |
| `editor/src/profiler_row_view_model.cpp` | Canvas row and its select method | [UI Profiler](../features/UI%20Profiler.md) |
| `editor/src/profiler_row_view_model.h` | `ProfilerRowViewModel` | [UI Profiler](../features/UI%20Profiler.md) |
| `editor/src/profiler_view_model.cpp` | Binds the view-model to `profiler.xml` and registers the paints | [UI Profiler](../features/UI%20Profiler.md) |
| `editor/src/profiler_view_model.h` | `ProfilerViewModel` | [UI Profiler](../features/UI%20Profiler.md) |
| `editor/src/project_build.cpp` | Configure and build through `ProcessCall`, the module record | [Editor](../features/Editor.md) |
| `editor/src/project_build.h` | `ProjectBuild`, `BuildSetup`, `configured_for` | [Editor](../features/Editor.md) |
| `editor/src/toolbar.cpp` | Play/Stop method and shown state | [Editor](../features/Editor.md) |
| `editor/src/toolbar.h` | `Toolbar`, `EditorRequest`, `RunState` | [Editor](../features/Editor.md) |
| `editor/src/ui_element_selection.h` | `UiElementSelection` | [Editor](../features/Editor.md#inspector) |
| `editor/src/ui_tree_panel.cpp` | Copies the inspector probe's tree into the view-model; a new probe selection becomes the editor's | [UI Inspector](../features/UI%20Inspector.md) |
| `editor/src/ui_tree_panel.h` | `UiTreePanel` | [UI Inspector](../features/UI%20Inspector.md) |
| `editor/src/ui_tree_row_view_model.cpp` | Tree row label, depth, expanded, select and toggle methods | [UI Inspector](../features/UI%20Inspector.md) |
| `editor/src/ui_tree_row_view_model.h` | `UiTreeRowViewModel` | [UI Inspector](../features/UI%20Inspector.md) |
| `editor/src/ui_tree_view_model.cpp` | Binds the view-model to `ui_tree.xml` | [UI Inspector](../features/UI%20Inspector.md) |
| `editor/src/ui_tree_view_model.h` | `UiTreeViewModel` | [UI Inspector](../features/UI%20Inspector.md) |
| `editor/tests/asset_inspection_test.cpp` | Folder, text, meta, broken meta, PNG, cut text, empty and gone files | [Editor](../features/Editor.md#inspector) |
| `editor/tests/build_panel_test.cpp` | Log tones, first error, scroll, line cap | [Editor](../features/Editor.md) |
| `editor/tests/dock_layout_file_test.cpp` | Layout file round trip, missing, corrupt, empty path | [Editor](../features/Editor.md#layout-file) |
| `editor/tests/editor_cli_test.cpp` | `EditorCli` replies and the requests it records | [Editor](../features/Editor.md#wind-cli) |
| `editor/tests/editor_options_test.cpp` | `--project` and `--play` | [Editor](../features/Editor.md) |
| `editor/tests/editor_panels_test.cpp` | Default layout, dock area and panel canvases, visible-only refresh, the shared selection, show, layout save and load, tree keys, attach and detach; OS float windows, saved float restore | [Editor](../features/Editor.md#panels) |
| `editor/tests/inspector_panel_test.cpp` | Inspector: nothing, a file read on select, collapsed sections, a UI element, detach | [Editor](../features/Editor.md#inspector) |
| `editor/tests/play_session_test.cpp` | Play, Stop order, play again, refusals, against the fixture module | [Editor](../features/Editor.md) |
| `editor/tests/profiler_chart_test.cpp` | Chart geometry and the chart paint | [UI Profiler](../features/UI%20Profiler.md) |
| `editor/tests/profiler_panel_test.cpp` | Profiler view-model with and without frames | [UI Profiler](../features/UI%20Profiler.md) |
| `editor/tests/project_build_test.cpp` | `ProjectBuild` against a scripted launcher | [Editor](../features/Editor.md) |
| `editor/tests/ui_tree_panel_test.cpp` | UI Tree view-models and the selection they publish, against a headless world | [UI Inspector](../features/UI%20Inspector.md) |
| `bench/CMakeLists.txt` | `wind_ui_bench` and `wind_ui_bench_tests` | [UI Bench](../features/UI%20Bench.md) |
| `bench/assets/**` | Bench scenes: documents, stylesheets (hover and paint-mix and motion variants), two generated textures | [UI Bench](../features/UI%20Bench.md#scenes) |
| `bench/src/bench_app.*`, `bench/src/main.cpp` | `BenchApp`: canvas, profiler, owned pointer, the run; `main` parses and hosts it | [UI Bench](../features/UI%20Bench.md) |
| `bench/src/bench_matrix.*`, `bench/src/bench_options.*`, `bench/src/bench_schedule.*`, `bench/src/frame_plan.*`, `bench/src/bench_report.*` | Matrix, command line, measurement schedule, per-mode frame plan, report JSON | [UI Bench](../features/UI%20Bench.md) |
| `bench/src/*_scene.*`, `bench/src/*_view_model.*`, `bench/src/*_paint.*` | One scene class per scene, its view-models and paints | [UI Bench](../features/UI%20Bench.md#scenes) |
| `bench/src/*_data.*`, `bench/src/bench_random.*`, `bench/src/scene_points.*` | Seeded data, SplitMix64, rest/wheel/hover points from the laid-out tree | [UI Bench](../features/UI%20Bench.md) |
| `bench/tests/bench_test.cpp` | `wind_ui_bench_tests` | [UI Bench](../features/UI%20Bench.md#tests) |
| `bench/run_matrix.ps1`, `bench/compare.ps1` | Run the matrix into `summary.md` / `summary.json`; compare two summaries | [UI Bench](../features/UI%20Bench.md#matrix-runs) |
| `bench/results/baseline/summary.*`, `bench/results/.gitignore` | The step-0 baseline; every other result is ignored | [UI Bench](../features/UI%20Bench.md#matrix-runs) |
| `launcher/CMakeLists.txt` | `wind_launcher` and `wind_launcher_tests` | [Launcher](../features/Launcher.md) |
| `launcher/assets/css/launcher.css` | Launcher window style | [Launcher](../features/Launcher.md) |
| `launcher/assets/css/launcher.css.meta` | Its GUID sidecar | [Assets](../features/Assets.md) |
| `launcher/assets/ui/launcher.xml` | Launcher window: navbar, Projects and SDKs pages, the SDK row menu | [Launcher](../features/Launcher.md) |
| `launcher/assets/ui/launcher.xml.meta` | Its GUID sidecar | [Assets](../features/Assets.md) |
| `launcher/src/launcher_app.cpp` | `LauncherApp`: lists, dialogs, requests, Open | [Launcher](../features/Launcher.md) |
| `launcher/src/launcher_app.h` | `LauncherApp` | [Launcher](../features/Launcher.md) |
| `launcher/src/launcher_request.h` | `LauncherRequest` | [Launcher](../features/Launcher.md) |
| `launcher/src/launcher_state.cpp` | `launcher.txt` parse, format, load, save; remember and forget | [Launcher](../features/Launcher.md) |
| `launcher/src/launcher_state.h` | `LauncherState` | [Launcher](../features/Launcher.md) |
| `launcher/src/launcher_view_model.cpp` | Binds the view-model to `launcher.xml` | [Launcher](../features/Launcher.md) |
| `launcher/src/launcher_view_model.h` | `LauncherViewModel` | [Launcher](../features/Launcher.md) |
| `launcher/src/main.cpp` | `ENGINE_GAME(launcher::LauncherApp)` | [Launcher](../features/Launcher.md) |
| `launcher/src/method_command.h` | `MethodCommand`, as in the editor | [Launcher](../features/Launcher.md) |
| `launcher/src/project_entry.cpp` | Reads one remembered project | [Launcher](../features/Launcher.md) |
| `launcher/src/project_entry.h` | `ProjectEntry` | [Launcher](../features/Launcher.md) |
| `launcher/src/project_template.cpp` | Target name, new-project checks, copying a template with placeholders | [Launcher](../features/Launcher.md#new-project) |
| `launcher/src/project_template.h` | `NewProject`, `project_target`, `new_project_problem`, `create_project` | [Launcher](../features/Launcher.md#new-project) |
| `launcher/src/project_row_view_model.cpp` | Project row, Open and Remove | [Launcher](../features/Launcher.md) |
| `launcher/src/project_row_view_model.h` | `ProjectRowViewModel` | [Launcher](../features/Launcher.md) |
| `launcher/src/sdk_catalog.cpp` | Install directory, finds and deletes SDKs, version order, the editor and file manager command lines | [Launcher](../features/Launcher.md) |
| `launcher/src/sdk_catalog.h` | `SdkEntry`, `sdk_install_directory`, `find_sdks`, `delete_sdk`, `sdk_for` | [Launcher](../features/Launcher.md) |
| `launcher/src/sdk_option_view_model.cpp` | One SDK in the New project picker | [Launcher](../features/Launcher.md#new-project) |
| `launcher/src/sdk_option_view_model.h` | `SdkOptionViewModel` | [Launcher](../features/Launcher.md#new-project) |
| `launcher/src/user_paths.cpp` | Environment paths, the default new-project location | [Launcher](../features/Launcher.md#new-project) |
| `launcher/src/user_paths.h` | `environment_path`, `default_project_location` | [Launcher](../features/Launcher.md#new-project) |
| `launcher/src/sdk_row_view_model.cpp` | SDK row: the ··· menu and the delete confirmation | [Launcher](../features/Launcher.md) |
| `launcher/src/sdk_row_view_model.h` | `SdkRowViewModel` | [Launcher](../features/Launcher.md) |
| `launcher/tests/launcher_test.cpp` | State, SDK catalog and delete, project entries | [Launcher](../features/Launcher.md) |
