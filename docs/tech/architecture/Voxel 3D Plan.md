# Voxel 3D plan

This is a plan. It is not a description of the engine as it runs today. It lists the engine work a block-world 3D multiplayer game (chunked voxel terrain, first-person movement, dedicated server) needs from Wind. Task numbers continue after `wind-298`; renumber if another task takes a number first.

## What stays in the game

Voxels are a genre, not an engine feature ([Principles](Principles.md) #8). The game owns block types and their data, chunk storage, the chunk mesher (culled or greedy faces, baked light and AO into vertices), world generation, the region save format, the block and chunk protocol on top of the net layer, inventory, mobs, and crafting.

The engine owns everything below that which any 3D or networked game would reuse: meshes that change, culling, texture arrays and mipmaps, a lit shader that takes per-vertex color and fog, mouse lock, grid queries, background jobs, compression, a tick-based net model with prediction and interest, a windowless server, and positional audio.

## As built today (the gaps)

Checked against `include/engine/` at `53bf8c5`:

- ~~`IMesh` has no update.~~ Done in wind-299: `IMesh::update` ([Render](../modules/Render.md#mesh-updates)).
- ~~`MeshVertex` is position, normal, uv. No vertex color, so baked block light and AO have nowhere to go.~~ Done in wind-300: `MeshVertex::color` ([Render](../modules/Render.md#meshes)).
- ~~`run_render` culls only tilemap chunks (`tilemap_chunk_visible`). Every `Renderable` is drawn.~~ Done in wind-303: `IMesh::bounds` and `Frustum` ([Frustum culling](../modules/Render.md#frustum-culling)).
- ~~`TextureDesc` has `filter` (`Nearest`, `Linear`) and `wrap`. No mipmaps, no texture arrays. A block atlas shimmers at distance and bleeds between tiles.~~ Done in wind-301: `mipmaps` and `texture_array` ([Mipmaps](../modules/Render.md#mipmaps), [Texture arrays](../modules/Render.md#texture-arrays)).
- ~~No alpha-test (cutout) render state, no fog,~~ no sky. Cutout and fog done in wind-300 ([Cutout](../modules/Render.md#cutout), [Fog](../modules/Render.md#fog)).
- ~~`IWindowControl::capture_mouse` is `SDL_CaptureMouse` (drag outside the window). There is no relative mode: the cursor is not hidden or locked for a first-person camera.~~ Done in wind-302: `set_relative_mouse` and `MouseAxis` bindings ([Windowing](../features/Windowing.md#relative-mouse), [Input Mapper](../features/Input%20Mapper.md#axes-mouse-look-and-composites)).
- ~~`physics_2d` only. No 3D collision or grid raycast.~~ Grid collision and raycast done in wind-304: `sweep_aabb` and `raycast_grid` ([Grid queries](../modules/ECS.md#grid-queries)). Still no 3D rigid-body physics.
- ~~`JobSystem` is fork-join `parallel_for` only. A chunk mesh built over several frames has no home.~~ Done in wind-305: `JobSystem::submit` and `JobCall` ([Async jobs](../modules/Core.md#async-jobs)).
- ~~No compression library.~~ Done in wind-306: LZ4 behind `<engine/core/compression.h>` ([Compression](../modules/Core.md#compression)).
- Net: `run_network_server_replication` sends every `NetIdentity` + `Transform` to every client each call, unreliable, with no tick number, no baseline, no relevance. `setup_client_snapshot_receiver` finds each entity by a linear `view<NetIdentity>()` scan per id instead of `NetEntityMap`. Interpolation is a `lerp_speed` chase, not a time-buffered snapshot. There is no client input channel and no prediction. [Net](../modules/Net.md) still calls `NetId` a component; in code it is `using NetId = std::uint32_t` and the component is `NetIdentity`.
- ~~`Engine<GameT>::init` always opens a window. There is no dedicated server run mode. `GameNetworkingSockets` is linked from a prebuilt Windows x64 `.lib` only.~~ Done in wind-310: `ServerHost` and `ENGINE_GAME_SERVER` ([Core](../modules/Core.md#dedicated-server)), GameNetworkingSockets from source for a Linux server.
- `IAudioSystem` has no position: `play_sfx(sound, volume)` only.

## Order

Phase 1 unblocks a single-player prototype. Phase 2 makes it playable at scale. Phase 3 is multiplayer. Phase 4 is polish. Tasks inside a phase are independent unless noted.

| Phase | Tasks |
| --- | --- |
| 1. Prototype | wind-299 mutable meshes, wind-300 vertex color + cutout + fog, wind-301 mipmaps + texture arrays, wind-302 relative mouse |
| 2. Scale | wind-303 frustum culling, wind-304 grid queries, wind-305 async jobs, wind-306 compression |
| 3. Multiplayer | wind-307 net cleanup + ticks, wind-308 input and prediction, wind-309 interest + bandwidth, wind-310 dedicated server |
| 4. Polish | wind-311 positional audio, wind-312 sky, later: shadows, glTF + skinning, instancing |

---

## Phase 1: prototype

### wind-299 feat: mutable meshes

Status: done. `IMesh::update` is non-virtual (validation, keep-old rule) over a protected backend `replace`; Vulkan retires replaced buffers until their frames complete; tilemap chunk edits update the chunk mesh. See [Render](../modules/Render.md#mesh-updates).

Problem: a block edit rebuilds one chunk mesh. Today that is a fresh `IMesh` each time.

Do:
- `IMesh::update(const MeshDesc&) -> bool` (or an `IDynamicMesh` beside `IDynamicTexture`, pick one and use it everywhere). Same validation as `create_mesh` (`mesh_desc_error`); on failure the old contents stay.
- OpenGL: reuse the buffers when the new data fits, `glBufferData` otherwise. VAOs per context stay valid (the element binding is VAO state; rebind on reallocation). Vulkan: same through its staging path, without stalling a frame in flight.
- `Tilemap::set_tile` uses it instead of rebuilding its chunk mesh.

Done when: `engine_tests` cover validation and the keep-old-on-failure rule through a recording factory; Render.md updated; tilemap edits go through `update`.

### wind-300 feat: vertex color, cutout and fog in the lit path

Status: done. `MeshVertex::color` at location 3 (`.mesh` 12-column rows), read by both builtin lit and unlit shaders; `RenderState::alpha_cutoff` (`.mat` `alpha_cutoff`) as a uniform with an always-present `discard`, opaque queue kept; linear `Fog` component on the active camera into `CmdSetView::fog`. Particles, UI, and `render_world_viewport` are not fogged. See [Render](../modules/Render.md#cutout).

Problem: baked light and AO need a per-vertex color; leaves and glass need alpha-test; distance needs fog to hide the load edge.

Do:
- `MeshVertex::color` (`glm::vec4`, default white) at `location = 3`. `.mesh` files and `make_*` primitives write white. Shaders that do not read it are unchanged.
- `builtin::shader_lit` multiplies albedo by vertex color.
- `RenderState::alpha_cutoff` (`std::optional<float>`): the lit and unlit shaders discard below it; such a material stays in the opaque queue.
- `CmdSetView::fog` (`std::optional<Fog>`: color, start, end, linear). Lit and unlit apply it by view distance.
- GLSL 330, GLES 300 (`shader_adapt`), Slang/SPIR-V.

Done when: tests cover the `.mesh` round trip with color, sort queue for cutout materials, and that a default `CmdSetView` has no fog; Render.md and the `wind-rendering` skill updated.

### wind-301 feat: mipmaps and texture arrays

Status: done. `MipmapMode` (`.meta` `mipmaps = "none" | "nearest" | "linear"`) on `TextureDesc` and `TextureArrayDesc`, `glGenerateMipmap` and a Vulkan blit chain; `create_texture_array` and `ITexture::layers()`; the layer is `MeshVertex::layer` at location 4 (not `uv.z`); `builtin::shader_lit_array` and `shader_unlit_array`; `importer = "texture_array"` makes one layer per `[[sprites]]` frame, addressed by name through `get<render::TextureArray>`. A mismatched albedo kind samples white. Limits: 256 layers, no `.mesh` layer column, an array PNG is not also a sprite sheet. See [Render](../modules/Render.md#texture-arrays).

Problem: a nearest-filtered block atlas aliases at distance; mipmapping an atlas bleeds neighbours. The usual answer is one array layer per block face.

Do:
- `TextureDesc::mipmaps` and a `.meta` key; `FilterMode` gains the mip choice (or a separate `MipFilter`), wired from `.meta` to both backends.
- `TextureArrayDesc` (width, height, layers, rgba per layer, mipmaps) and `create_texture_array` on `IGraphicFactory`. A material binds it like a 2D texture; shaders sample `sampler2DArray` with the layer in `uv`'s third component or a separate vertex attribute (decide once, document it).
- An importer for an array from a sprite sheet: one layer per frame, so the game addresses layers by frame name through `AssetsDb`.

Done when: the importer and `.meta` parsing are tested; WebGL2 / GLES 3.0 paths compile (`sampler2DArray` is core there); Assets.md and Render.md updated.

### wind-302 feat: relative mouse mode

Status: done. `IWindowControl::set_relative_mouse` / `is_relative_mouse` over `RelativeMouse` (focus loss releases, focus gain restores, Stop releases through `reset_game_windows`); Web pointer lock needs a user gesture; Android is a no-op. `ControlKind::MouseMotion` with `MouseAxis::X`, `Y`, `Wheel`, a `scale` on every binding, and `InputSystem::axis` (per-frame motion, reset by `begin_frame` before `poll`); WASD composites are signed scales. `MouseEvent::locked` keeps the UI and pointer targets off the hidden cursor. Gamepad poll and bind stays in the Scope backlog. See [Windowing](../features/Windowing.md#relative-mouse) and [Input Mapper](../features/Input%20Mapper.md#axes-mouse-look-and-composites).

Problem: a first-person camera needs a hidden, locked cursor and raw deltas.

Do:
- `IWindowControl::set_relative_mouse(WindowId, bool)` and `is_relative_mouse`. SDL `SDL_SetWindowRelativeMouseMode`; Web pointer lock (needs a user gesture, document it); a no-op on Android.
- Released on focus loss and on Stop in the editor (`detach_game`), restored on focus gain if the game asked for it.
- A mouse-delta input control so `InputSystem::bind` can map look to an `ActionId` axis, not `MouseEvent` in a system ([Principles](Principles.md) #4). This overlaps the backlog's WASD composites and gamepad wiring; do those here if they are still open.

Done when: input tests cover the delta axis and focus-loss release with a fake window control; Input Mapper.md and Windowing.md updated.

## Phase 2: scale

### wind-303 feat: frustum culling for meshes

Status: done. `IMesh::bounds()` is computed in the base (`explicit IMesh(const MeshDesc&)`, refreshed by a successful `update`; `mesh_bounds`); `Aabb`, `transform_aabb` (conservative OBB to AABB) and `Frustum` (`make_frustum`, `frustum_intersects`, `visible_in_any`) are public in `<engine/render/aabb.h>` and `<engine/render/frustum.h>`, plus `camera_frustum` in `<engine/ecs/camera.h>`. `run_render` keeps a `Renderable` or `Sprite` (under its final sprite model) that any bound window's frustum sees, one shared list; `render_world_viewport` culls with its own camera; tilemap chunks use the same test and `tilemap_chunk_visible` is gone. Particles are not culled. Opt-out: `frustum_cull = false` on `Renderable` and `Sprite`, serialized. See [Render](../modules/Render.md#frustum-culling).

Do:
- `IMesh::bounds()` (local AABB, computed from the desc on create and update).
- A `Frustum` from view-projection with an AABB test, public in `<engine/ecs/camera.h>` or its own header, reused by `tilemap_chunk_visible`.
- `run_render` and `render_world_viewport` skip `Renderable`s outside every bound window's frustum (transform the box by `WorldTransform`).

Done when: frustum math is tested (inside, outside, straddling, ortho and perspective) and a culled entity pushes no `CmdDrawMesh`.

### wind-304 feat: grid queries

Status: done. `<engine/ecs/grid_query.h>`, pure math in namespace `engine`: `raycast_grid` (Amanatides-Woo; half-open boundaries in the direction of travel; start inside reports `t = 0` and a zero normal; an exact edge or corner is blocked by any touching solid cell) and `sweep_aabb` (Y, X, Z per sub-step, at most 64 sub-steps of up to one cell, `kGridSkin` = 1e-3 so a resting box stays grounded without drift), each with a 2D overload over `glm::ivec2`. `is_solid` is a `CellPredicate`, a non-owning callable reference valid only for the call. Unit cells in grid space, no cell-size parameter. No `Tilemap` helper: a layer is a lambda over `Tilemap::tile` (row `-y - 1`). See [ECS](../modules/ECS.md#grid-queries).

Problem: block worlds, and 2D tile games, need collision against a solid-cell grid and a ray through cells. This is generic; a rigid-body 3D physics engine is not needed for it.

Do (pure math, header `<engine/ecs/grid_query.h>` or similar, no ECS dependency):
- `raycast_grid(origin, dir, max_distance, is_solid) -> std::optional<GridHit>` (Amanatides–Woo DDA): cell, entry face normal, distance.
- `sweep_aabb(box, delta, is_solid) -> SweepResult`: axis-separated move, resolved position, which axes hit (grounded on -Y).
- `is_solid` is a callable over `glm::ivec3`; a 2D overload over `glm::ivec2`. Optionally wire the 2D one to `Tilemap` as the Scope's missing tile collision.

Done when: `engine_tests` cover rays along axes and diagonals, start inside a solid cell, max distance, sliding along walls, step-free stair blocking, and the corner case of moving exactly along an edge.

### wind-305 feat: async jobs with a main-thread result

Status: done. `JobSystem::submit(fn, priority) -> JobCall<T>` (`<engine/core/job_call.h>`): `fn` is `T()` or `T(const JobToken&)`, delivered by `JobSystem::poll`, which `GameLoop::tick` calls right after `ProcessLauncher::poll`. Higher priority first, `set_priority` moves a queued job. Cancel or drop: queued never runs, running sees its token and its result is discarded. Same workers as `parallel_for`, at most `workers - 1` jobs at once; with no workers (Web without pthreads) `poll` runs jobs inline within a 4 ms budget. A throw is `std::terminate`. Editor Stop calls `JobSystem::cancel_all` after `wind_destroy_game`, which waits for running jobs before the unload. See [Core](../modules/Core.md#async-jobs).

Problem: generating and meshing a chunk takes longer than a frame. `parallel_for` blocks until done.

Do:
- `JobSystem::submit(fn) -> JobCall<T>`: runs `fn` on a worker, the result arrives on the main thread in the game loop's poll, like `HttpCall` (reuse `CallCompletions`). Dropping or cancelling the call discards the result. Ordered priority (nearest chunk first) via a priority argument or a re-sortable queue.
- The worker function follows [Principles](Principles.md) #12: it returns data (`MeshDesc`, a block array), the main thread calls `create_mesh` / `update`.
- In the editor, pending calls from the game die with the module on Stop.

Done when: tests cover delivery only on poll, cancel before and during the run, drop-on-destroy, and priority order.

### wind-306 feat: compression

Status: done. LZ4 `v1.10.0` (`external/lz4`, BSD-2-Clause), built from `lib/lz4.c` and `lib/lz4hc.c` as the PRIVATE static `wind_lz4`. `compress(span, CompressionLevel::Fast | High) -> vector` and `decompress(span, max_size) -> expected<vector, CompressionError>`: a frozen 5-byte header (format byte, u32 uncompressed size), stored as-is when LZ4 does not shrink the input, `max_size` checked before any allocation, no checksum. Pure and callable from job workers, the exception to [Principles](Principles.md) #12. See [Core](../modules/Core.md#compression).

Do:
- One fast codec as a submodule (LZ4 or zstd, PRIVATE), behind `<engine/core/compression.h>`: `compress(span) -> vector`, `decompress(span, max_size) -> expected<vector, CompressionError>`. No codec types in the public header.
- Used by the game's region saves and chunk packets; the net layer may use it later for large reliable messages.

Done when: round trip, corrupt input, and the `max_size` guard are tested; CMake.md lists the new submodule.

## Phase 3: multiplayer

### wind-307 refactor: net cleanup and server ticks

Do:
- Fix the docs drift (`NetId` alias, `NetIdentity` component) and make the snapshot receiver look entities up through `NetEntityMap`.
- A server tick counter on the fixed schedule; every snapshot carries its tick. The client keeps a small time-ordered snapshot buffer and interpolates between two snapshots at `render_time = server_time - interpolation_delay`, replacing `lerp_speed`.
- Snapshot content becomes a registry of replicated components (`register_replicated<T>(write, read)`), not a hard-coded `Transform`. `Transform` and `Parent` are the first two.
- Delta against the last snapshot the client acknowledged; full snapshot when there is no acked baseline.

Done when: loopback-transport tests cover tick ordering, out-of-order and dropped snapshots, interpolation between buffered ticks, a custom replicated component, and delta against an acked baseline.

Done in wind-307: `NetServer` / `NetClient` on the new `NetReceive` / `NetSend` phases, `ReplicationRegistry`, delta snapshots against the acked baseline, spawn and despawn on the interpolation timeline, `PacketDispatcher` and `ReplicatedTransform` removed, `DeliveryMode::UnreliableSequenced` renamed `UnreliableImmediate` ([Net](../modules/Net.md#realtime)).

### wind-308 feat: client input and prediction

Do (generic over the game's input and state types):
- An input channel: the client sends `(tick, input)` per fixed step, redundantly (last N inputs in each packet); the server applies them in tick order and acks the last processed tick in its snapshot.
- `Prediction<Input, State>`: ring buffer of inputs and predicted states; on a server state for tick T it compares, and on mismatch rewinds to T and replays inputs with the game's own step function.
- Server-side input buffer with a small jitter delay and a policy for missing ticks (repeat last input).

Done when: loopback tests with simulated latency and loss show reconciliation converging and no replay when predictions match.

Done in wind-308: `register_prediction<Input, State>` with a `PredictionDesc` (wire format, `step`, `collect`, `capture`, `restore`, `matches`, `corrected`) on both sides; one tick numbering with the client ahead; the Input message (`0x0103`, last `input_redundancy` inputs); the server's per-entity `InputBuffer` (first copy wins, repeat last, lead cap, ownership check) stepping controlled entities from `NetReceive`; per-peer `input_tick` and `input_slack` in the snapshot header (protocol 2); `InputLead` dilating the client's `Time::fixed_rate` from that slack, with forward jumps only; reconciliation against the newest snapshot that replays on a mismatch; predicted entities skipped by interpolation ([Net](../modules/Net.md#input-and-prediction)).


### wind-309 feat: interest management and bandwidth

Do:
- Per-peer relevance: the game supplies a relevance function (or a cell size for a spatial grid); an entity entering relevance is spawned on that peer, leaving it is despawned.
- A per-peer send budget per tick: snapshots first, then queued reliable payloads (chunk data) in priority order, so streaming terrain never starves movement.
- Fragmentation for reliable messages above the transport limit, if GameNetworkingSockets' own limit is in the way at the chosen chunk size.

Done when: tests cover enter/leave relevance spawn/despawn, budget ordering, and reassembly of a fragmented message over the loopback transport.

Done in wind-309: `NetInterest` (spatial grid over root positions with a leave margin, a `relevant` filter, a `priority` weight) and `NetAlwaysRelevant`, with owned entities always relevant and a relevant child pulling in its ancestors; per-peer, per-entity acks (`PeerGhosts`) replacing the whole-state baseline, because a snapshot that leaves entities out would take them back to the baseline with others in flight; a client state per tick derived from the one before it, last-write-wins in tick order; `snapshot_budget_bytes` and a priority accumulator so a large relevant set fits one packet, with predicted entities never deferred; `peer_bytes_per_second` with snapshots first; `NetServer::send_payload` with priority plus age, cancel, progress, `NetPayloadDeliveredEvent`, 1024-byte fragments, the transport backlog limit (`pending_send_bytes`), and a `compressed` flag the client decompresses under `max_payload_bytes`. Protocol 3 ([Interest](../modules/Net.md#interest), [Budget](../modules/Net.md#budget), [Reliable payloads](../modules/Net.md#reliable-payloads)).

### wind-310 feat: dedicated server

Do:
- A windowless run mode: `EngineHost` without `open_primary`, fixed-step loop that sleeps to the tick rate, no render, UI, or audio systems registered (the headless contract in Core.md already covers most of it). Selected by the game entry (`ENGINE_GAME_SERVER(...)` or a run option), not by `ENGINE_EDITOR`.
- Command-line port and config, log to stdout, clean shutdown on SIGINT / Ctrl+C.
- GameNetworkingSockets for Linux x64: build it from the submodule instead of a prebuilt Windows `.lib`, so a server runs on a Linux host.
- `engine_add_game` (or a sibling `engine_add_server`) in `cmake/wind_game.cmake` for both the source build and the SDK.

Done when: a `Host`-based test runs a server world for N ticks with no window; a Linux CI configure builds the server target; Game Consumer.md documents it.

Done in wind-310: `ENGINE_GAME_SERVER` and `engine_add_server`, a separate executable with no SDL. Not `EngineHost` without `open_primary` as written above, because `EngineHost` needs SDL video and `EngineServices` holds window references: `ServerHost` (always compiled) with `ServerServices`, over the same `GameLoop` through `ServerPresentation`, which sleeps to `kFixed` (a high-resolution timer on Windows) and warns about dropped ticks. `--port`, `--config`, `--assets`, the rest to the game; a TOML config; log on stdout; exit codes 0, 1, 2. SIGINT, SIGTERM, SIGHUP, and the Windows console events stop it with `on_quit`. The test is not `Host`-based, because `Host` enables UI and audio: real-loop `ServerHost` tests, and a real process with a client over a real socket. `<sdk>/player/Server` is a third player flavour without SDL, for Windows and Linux; `engine_add_server` works beside a game module in one tree. GameNetworkingSockets on Linux is the `update = none` submodule `external/GameNetworkingSockets-src` at `v1.6.0` with the system's static protobuf and OpenSSL, for the server only; Windows keeps the prebuilt. `GnsBaseTransport::stop` now closes connections before the listen socket, so a peer hears the close. The Linux path has the CI job `linux-server` and has not run yet. See [Core](../modules/Core.md#dedicated-server), [CMake](../build/CMake.md#engine_add_server), [Net](../modules/Net.md#gamenetworkingsockets-builds).

## Phase 4: polish

### wind-311 feat: positional audio

Do: `play_sfx_at(sound, position, volume)` and a position on looping SFX handles; the listener follows `ActiveCamera` (or an explicit `AudioListener` component); distance attenuation and stereo pan through SDL3_mixer's 3D position API. Tests with the fake mixer check attenuation and pan math.

### wind-312 feat: sky

Do: a sky draw before opaque world draws (gradient or cubemap material), its color shared with fog so the horizon matches.

### Later

Not needed for a first playable build, already in the Scope backlog: shadows (one directional cascade), glTF import and skinned meshes (mobs, player model), instancing (dropped items, particles of blocks), post-processing.

## See also

- [Scope](Scope.md)
- [Principles](Principles.md)
- [Net](../modules/Net.md)
- [Render](../modules/Render.md)
