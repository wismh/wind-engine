# Voxel 3D plan

This is a plan. It is not a description of the engine as it runs today. It lists the engine work a block-world 3D multiplayer game (chunked voxel terrain, first-person movement, dedicated server) needs from Wind. Task numbers continue after `wind-298`; renumber if another task takes a number first.

## What stays in the game

Voxels are a genre, not an engine feature ([Principles](Principles.md) #8). The game owns block types and their data, chunk storage, the chunk mesher (culled or greedy faces, baked light and AO into vertices), world generation, the region save format, the block and chunk protocol on top of the net layer, inventory, mobs, and crafting.

The engine owns everything below that which any 3D or networked game would reuse: meshes that change, culling, texture arrays and mipmaps, a lit shader that takes per-vertex color and fog, mouse lock, grid queries, background jobs, compression, a tick-based net model with prediction and interest, a windowless server, and positional audio.

## As built today (the gaps)

Checked against `include/engine/` at `53bf8c5`:

- ~~`IMesh` has no update.~~ Done in wind-299: `IMesh::update` ([Render](../modules/Render.md#mesh-updates)).
- `MeshVertex` is position, normal, uv. No vertex color, so baked block light and AO have nowhere to go.
- `run_render` culls only tilemap chunks (`tilemap_chunk_visible`). Every `Renderable` is drawn.
- `TextureDesc` has `filter` (`Nearest`, `Linear`) and `wrap`. No mipmaps, no texture arrays. A block atlas shimmers at distance and bleeds between tiles.
- No alpha-test (cutout) render state, no fog, no sky.
- `IWindowControl::capture_mouse` is `SDL_CaptureMouse` (drag outside the window). There is no relative mode: the cursor is not hidden or locked for a first-person camera. `MouseEvent::relative` already carries the delta.
- `physics_2d` only. No 3D collision or grid raycast.
- `JobSystem` is fork-join `parallel_for` only. A chunk mesh built over several frames has no home.
- No compression library.
- Net: `run_network_server_replication` sends every `NetIdentity` + `Transform` to every client each call, unreliable, with no tick number, no baseline, no relevance. `setup_client_snapshot_receiver` finds each entity by a linear `view<NetIdentity>()` scan per id instead of `NetEntityMap`. Interpolation is a `lerp_speed` chase, not a time-buffered snapshot. There is no client input channel and no prediction. [Net](../modules/Net.md) still calls `NetId` a component; in code it is `using NetId = std::uint32_t` and the component is `NetIdentity`.
- `Engine<GameT>::init` always opens a window. There is no dedicated server run mode. `GameNetworkingSockets` is linked from a prebuilt Windows x64 `.lib` only.
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

Problem: baked light and AO need a per-vertex color; leaves and glass need alpha-test; distance needs fog to hide the load edge.

Do:
- `MeshVertex::color` (`glm::vec4`, default white) at `location = 3`. `.mesh` files and `make_*` primitives write white. Shaders that do not read it are unchanged.
- `builtin::shader_lit` multiplies albedo by vertex color.
- `RenderState::alpha_cutoff` (`std::optional<float>`): the lit and unlit shaders discard below it; such a material stays in the opaque queue.
- `CmdSetView::fog` (`std::optional<Fog>`: color, start, end, linear). Lit and unlit apply it by view distance.
- GLSL 330, GLES 300 (`shader_adapt`), Slang/SPIR-V.

Done when: tests cover the `.mesh` round trip with color, sort queue for cutout materials, and that a default `CmdSetView` has no fog; Render.md and the `wind-rendering` skill updated.

### wind-301 feat: mipmaps and texture arrays

Problem: a nearest-filtered block atlas aliases at distance; mipmapping an atlas bleeds neighbours. The usual answer is one array layer per block face.

Do:
- `TextureDesc::mipmaps` and a `.meta` key; `FilterMode` gains the mip choice (or a separate `MipFilter`), wired from `.meta` to both backends.
- `TextureArrayDesc` (width, height, layers, rgba per layer, mipmaps) and `create_texture_array` on `IGraphicFactory`. A material binds it like a 2D texture; shaders sample `sampler2DArray` with the layer in `uv`'s third component or a separate vertex attribute (decide once, document it).
- An importer for an array from a sprite sheet: one layer per frame, so the game addresses layers by frame name through `AssetsDb`.

Done when: the importer and `.meta` parsing are tested; WebGL2 / GLES 3.0 paths compile (`sampler2DArray` is core there); Assets.md and Render.md updated.

### wind-302 feat: relative mouse mode

Problem: a first-person camera needs a hidden, locked cursor and raw deltas.

Do:
- `IWindowControl::set_relative_mouse(WindowId, bool)` and `is_relative_mouse`. SDL `SDL_SetWindowRelativeMouseMode`; Web pointer lock (needs a user gesture, document it); a no-op on Android.
- Released on focus loss and on Stop in the editor (`detach_game`), restored on focus gain if the game asked for it.
- A mouse-delta input control so `InputSystem::bind` can map look to an `ActionId` axis, not `MouseEvent` in a system ([Principles](Principles.md) #4). This overlaps the backlog's WASD composites and gamepad wiring; do those here if they are still open.

Done when: input tests cover the delta axis and focus-loss release with a fake window control; Input Mapper.md and Windowing.md updated.

## Phase 2: scale

### wind-303 feat: frustum culling for meshes

Do:
- `IMesh::bounds()` (local AABB, computed from the desc on create and update).
- A `Frustum` from view-projection with an AABB test, public in `<engine/ecs/camera.h>` or its own header, reused by `tilemap_chunk_visible`.
- `run_render` and `render_world_viewport` skip `Renderable`s outside every bound window's frustum (transform the box by `WorldTransform`).

Done when: frustum math is tested (inside, outside, straddling, ortho and perspective) and a culled entity pushes no `CmdDrawMesh`.

### wind-304 feat: grid queries

Problem: block worlds, and 2D tile games, need collision against a solid-cell grid and a ray through cells. This is generic; a rigid-body 3D physics engine is not needed for it.

Do (pure math, header `<engine/ecs/grid_query.h>` or similar, no ECS dependency):
- `raycast_grid(origin, dir, max_distance, is_solid) -> std::optional<GridHit>` (Amanatides–Woo DDA): cell, entry face normal, distance.
- `sweep_aabb(box, delta, is_solid) -> SweepResult`: axis-separated move, resolved position, which axes hit (grounded on -Y).
- `is_solid` is a callable over `glm::ivec3`; a 2D overload over `glm::ivec2`. Optionally wire the 2D one to `Tilemap` as the Scope's missing tile collision.

Done when: `engine_tests` cover rays along axes and diagonals, start inside a solid cell, max distance, sliding along walls, step-free stair blocking, and the corner case of moving exactly along an edge.

### wind-305 feat: async jobs with a main-thread result

Problem: generating and meshing a chunk takes longer than a frame. `parallel_for` blocks until done.

Do:
- `JobSystem::submit(fn) -> JobCall<T>`: runs `fn` on a worker, the result arrives on the main thread in the game loop's poll, like `HttpCall` (reuse `CallCompletions`). Dropping or cancelling the call discards the result. Ordered priority (nearest chunk first) via a priority argument or a re-sortable queue.
- The worker function follows [Principles](Principles.md) #12: it returns data (`MeshDesc`, a block array), the main thread calls `create_mesh` / `update`.
- In the editor, pending calls from the game die with the module on Stop.

Done when: tests cover delivery only on poll, cancel before and during the run, drop-on-destroy, and priority order.

### wind-306 feat: compression

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

### wind-308 feat: client input and prediction

Do (generic over the game's input and state types):
- An input channel: the client sends `(tick, input)` per fixed step, redundantly (last N inputs in each packet); the server applies them in tick order and acks the last processed tick in its snapshot.
- `Prediction<Input, State>`: ring buffer of inputs and predicted states; on a server state for tick T it compares, and on mismatch rewinds to T and replays inputs with the game's own step function.
- Server-side input buffer with a small jitter delay and a policy for missing ticks (repeat last input).

Done when: loopback tests with simulated latency and loss show reconciliation converging and no replay when predictions match.

### wind-309 feat: interest management and bandwidth

Do:
- Per-peer relevance: the game supplies a relevance function (or a cell size for a spatial grid); an entity entering relevance is spawned on that peer, leaving it is despawned.
- A per-peer send budget per tick: snapshots first, then queued reliable payloads (chunk data) in priority order, so streaming terrain never starves movement.
- Fragmentation for reliable messages above the transport limit, if GameNetworkingSockets' own limit is in the way at the chosen chunk size.

Done when: tests cover enter/leave relevance spawn/despawn, budget ordering, and reassembly of a fragmented message over the loopback transport.

### wind-310 feat: dedicated server

Do:
- A windowless run mode: `EngineHost` without `open_primary`, fixed-step loop that sleeps to the tick rate, no render, UI, or audio systems registered (the headless contract in Core.md already covers most of it). Selected by the game entry (`ENGINE_GAME_SERVER(...)` or a run option), not by `ENGINE_EDITOR`.
- Command-line port and config, log to stdout, clean shutdown on SIGINT / Ctrl+C.
- GameNetworkingSockets for Linux x64: build it from the submodule instead of a prebuilt Windows `.lib`, so a server runs on a Linux host.
- `engine_add_game` (or a sibling `engine_add_server`) in `cmake/wind_game.cmake` for both the source build and the SDK.

Done when: a `Host`-based test runs a server world for N ticks with no window; a Linux CI configure builds the server target; Game Consumer.md documents it.

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
