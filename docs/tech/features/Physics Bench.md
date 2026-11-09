# Physics bench

`wind_physics_bench` is a headless command-line program in `bench/physics/`. It builds physics scenes, steps them a fixed number of ticks, and prints a Markdown table of microseconds per step. It has no window and no assets. It is built against the static engine when `ENGINE_PHYSICS_BENCH` is on, which is the default when this repo is the CMake root, so the `vs` preset builds it.

## Build and run

```bash
cmake --preset vs
cmake --build build --config RelWithDebInfo --target wind_physics_bench
build/bin/RelWithDebInfo/wind_physics_bench --2d --filter static_ --threads 0
```

Reference numbers come from RelWithDebInfo. A Debug engine (`/Od /RTC1`) is many times slower and does not show where the time goes. A game on an SDK builds `DebugGame` against a Release SDK, so its engine code is optimized as well.

| Flag | |
| --- | --- |
| `--2d`, `--3d`, `--all` | which suite (default both) |
| `--filter <substr>` | scenarios whose name contains the text, case-insensitive (alias `--scenario`) |
| `--steps N` | steps per scenario instead of the default |
| `--threads N` | `JobSystem` workers; 0 runs everything on the caller. Default is hardware threads minus one |
| `--skip-large`, `--full` | leave out, or run longer, `large_pyramid_100` |

## Static-terrain scenarios

`bench/physics/bench_static_2d.cpp`. They time `run_physics_2d` alone (no other fixed systems) after 240 warm-up ticks. The terrain is a 100-column grid below y = 0, half boxes (every seventh one rotated) and half convex polygons (quads and triangles), like marching-squares cells. Away from it: one platform carrying 4 dynamic boxes that are asleep before measuring starts, 4 awake dynamic boxes in free fall, and 2 kinematic boxes gliding above the terrain without touching it. No body touches the terrain, except in the `static_contact_*` rows. 600 steps.

| Scenario | What it shows |
| --- | --- |
| `static_far_2k`, `static_far_4k`, `static_far_8k` | 2,000, 4,000, 8,000 static colliders. The cost should not grow with the count |
| `static_far_2k_parented` | the same 2,000 cells as children of 9 chunk entities. Informational: `update_world_transforms` (three calls per tick) walks every child |
| `static_edit_2k` | every 30th tick one cell moves in place and `mark_static_colliders_dirty_2d(world, cell)` is called (LQ-28). The edit ticks and the other ticks are separate rows |
| `static_edit_2k_world` | the same edits, marked with the world-wide `mark_static_colliders_dirty_2d(world)`: every static collider is re-read. For comparison with the per-entity mark |
| `static_spawn_2k` | every 30th tick a dynamic box with a collider is spawned far away. The collider add re-reads the static cache that tick |
| `static_far_2k_compound`, `static_far_8k_compound` | the same cells as shapes of one `CompoundCollider2D` on each of 9 chunk entities (LQ-23): 10 static entities instead of 2,001 or 8,001 |
| `static_edit_2k_compound`, `static_edit_2k_compound_world`, `static_spawn_2k_compound` | `static_edit_2k`, `static_edit_2k_world` and `static_spawn_2k` on the compound terrain. The edit moves the vertices of one chunk shape in place, then marks the chunk (about 222 parts) or the world |
| `static_contact_2k`, `static_contact_2k_compound` | after warm-up, 64 dynamic unit boxes are dropped just above the terrain (separate cells, then compound chunks). Every tick from the drop is timed: landing, resting contacts on cell seams, and falling asleep |

## Reference numbers

Windows, MSVC, RelWithDebInfo, `--threads 0`, mean microseconds per step. "Before" is the engine before LQ-22 (every tick gathered every collider and compared the static list item by item).

| Scenario | Before | After |
| --- | ---: | ---: |
| `static_far_2k` | 1,289 | 35 |
| `static_far_4k` | 3,103 | 35 |
| `static_far_8k` | 6,064 | 34 |
| `static_far_2k_parented` | 3,749 | 2,107 |
| `static_edit_2k`, edit ticks | 2,628 | 735 |
| `static_edit_2k`, other ticks | 1,220 | 34 |
| `static_spawn_2k`, spawn ticks | 1,254 | 764 |
| `static_spawn_2k`, other ticks | 1,241 | 53 |
| `tile_world (2500 tiles + 100 movers)` | 2,784 | 910 |
| `tile_world`, 11 workers | 2,668 | 869 |

An edit or spawn tick re-reads all static colliders (one view per collider type) and edits the static AABB tree only for the colliders that changed. Its cost still follows the static count. The parented case is dominated by `update_world_transforms`, which this bench does not change.

### Compound terrain (LQ-23)

One run of the LQ-23 build, same settings. It was measured with a busy background process, so every row is about 1.75 times the table above, including rows whose code LQ-23 did not touch (`static_far_2k_parented` is `update_world_transforms`, `tile_world` has no compound). Compare the rows of this table with each other, not with the table above. "Static entities" counts the terrain entities plus the platform (and the 9 chunk parents in the parented case).

| Scenario | Static entities | Mean us/step |
| --- | ---: | ---: |
| `static_far_2k` | 2,001 | 62 |
| `static_far_8k` | 8,001 | 57 |
| `static_far_2k_parented` | 2,010 | 3,702 |
| `static_far_2k_compound` | 10 | 58 |
| `static_far_8k_compound` | 10 | 55 |
| `static_edit_2k`, edit ticks | 2,001 | 1,284 |
| `static_edit_2k_compound`, edit ticks | 10 | 844 |
| `static_edit_2k`, other ticks | 2,001 | 57 |
| `static_edit_2k_compound`, other ticks | 10 | 60 |
| `static_spawn_2k`, spawn ticks | 2,001 | 1,241 |
| `static_spawn_2k_compound`, spawn ticks | 10 | 813 |
| `static_spawn_2k`, other ticks | 2,001 | 92 |
| `static_spawn_2k_compound`, other ticks | 10 | 91 |
| `static_contact_2k` (64 boxes land, 68 of 74 bodies asleep at the end) | 2,001 | 1,616 |
| `static_contact_2k_compound` (same, 68 of 74 asleep) | 10 | 1,598 |
| `tile_world` | 2,500 | 1,588 |
| `tile_world`, 11 workers | 2,500 | 1,499 |

The compound terrain costs the same per idle tick as separate cells, and the contact path is the same. A re-read of the static cache is cheaper (one view walk over 9 entities, one matrix per chunk), and the hierarchy cost of the parented layout is gone: 9 chunk entities have no children to walk.

### Per-entity static marks (LQ-28)

New runs of the LQ-28 build, same machine and settings, two runs each (the range is the two means). "Before" is the same scenario on the `dev` build just before LQ-28, where the only mark was world-wide; it was measured in the same session. The `_world` rows run the new build with the world-wide mark, so they are the old path on the new code.

| Scenario | Before | After |
| --- | ---: | ---: |
| `static_edit_2k`, edit ticks | 833-1,014 | 48-62 |
| `static_edit_2k`, other ticks | 36-45 | 33-37 |
| `static_edit_2k_world`, edit ticks | | 984-1,068 |
| `static_edit_2k_compound`, edit ticks | 377-528 | 90-100 |
| `static_edit_2k_compound`, other ticks | 35 | 35-37 |
| `static_edit_2k_compound_world`, edit ticks | | 719-873 |

A marked cell edit now costs about one idle tick plus the re-read of that one cell; a marked chunk edit re-reads the chunk's ~222 parts instead of all 2,000. The world-wide mark is unchanged. The edit-tick rows are 20 ticks each and vary by about 20 % between runs.

### SDF terrain (LQ-26)

The `static_sdf_*` rows replace the cells with one entity carrying an `SdfCollider2D` over the same 100 x 20 block (0.25-unit cells, a 1-unit band, a 1.5-unit crater every 10 columns along the top): 56 allocated chunks, so 56 static cache slots, and 2 static entities with the platform. The terrain's top surface lies on a chunk row seam (y = 0), so a body resting on it meets two chunks.

| Scenario | What it measures |
| --- | --- |
| `static_sdf_far` | the field, nothing touching it |
| `static_sdf_edit`, `static_sdf_edit_world` | every 30th tick `carve_circle` (radius 0.6) bites into the top surface, then `mark_static_colliders_dirty_2d(world, field)` (or the world-wide mark). The carve itself runs before the timer, as the compound edit does; the edit tick is the re-read |
| `static_sdf_contact` | the 64 boxes of `static_contact_*` dropped on the field |

Two runs of the LQ-26 build, same machine and settings (the range is the two means), with the closest rows of the same runs:

| Scenario | Mean (us/step) |
| --- | ---: |
| `static_far_2k_compound` | 33 |
| `static_sdf_far` | 34 |
| `static_edit_2k_compound`, edit ticks | 73 |
| `static_sdf_edit`, edit ticks | 58-95 |
| `static_sdf_edit_world`, edit ticks | 48-67 |
| `static_sdf_edit`, other ticks | 40-56 |
| `static_contact_2k_compound` (68 of 74 asleep at the end) | 918-926 |
| `static_sdf_contact` (68 of 74 asleep at the end) | 2,124-2,146 |

An idle field costs what idle compound chunks cost. An edit re-read is cheap either way: it reads 56 chunk items (coordinates and revision, not samples) and moves only the leaves of chunks whose revision changed, so the world-wide mark costs the same as the per-entity one for a world with a single field. The second run had one 3.8 ms outlier tick in `static_sdf_edit` other ticks, which lifts its edit and other means. Contacts are about 2.3 times the compound terrain's: a unit box reads the field at 16 points (corners and edge points one cell apart) for every chunk pair, each of the ~11 contact evaluations per tick (events, wake, warm start, 8 position iterations), and a sleeping body on static terrain is still evaluated. Batching the reads per contact (`SdfField2D::sample_many`, one chunk lookup per run of points) and not zero-filling the scratch arrays brought this row from 3.6 ms to 2.1 ms; a coarser sample spacing would cut it further at the cost of thinner features slipping through.

## Particle collision (LQ-25)

`bench/physics/bench_particles_2d.cpp`, in the `--2d` suite. Each row times `run_particles` alone (one emitter, gather included) for 600 frames of 1/60 s, from the first frame. The particles are placed, not emitted: a square grid with a fixed spread of 2-unit/s velocities, `collision_radius` 0.05, `bounce` 0.2, `friction` 0.1, gravity -10, and a lifetime longer than the run. The "Bodies" column is the particle count. Stability: no particle lost, and on SDF rows none ends with `sample < 0`.

| Scenario | What it measures |
| --- | --- |
| `particles_free_4k` | 4,096 particles, `collision_enabled` off: integrate alone |
| `particles_box_floor_4k` | the same over one `BoxCollider2D` floor: they land and rest on it |
| `particles_sdf_far_4k` | the static-terrain SDF field (100 x 20, 0.25-unit cells, 1-unit band, craters), particles 200 units away: the bounds reject only |
| `particles_sdf_terrain_4k`, `particles_sdf_terrain_16k` | 4,096 or 16,384 particles dropped from 1-9 units onto that field: falling, landing, resting and sliding into the craters |
| `particles_sdf_pool_4k` | a `Solid` world with one radius-8 cavity holding 4,096 particles: a pool where every particle ends up on the surface |

Windows, MSVC, RelWithDebInfo, mean microseconds per frame, ns per particle in brackets:

| Scenario | `--threads 0` | `--threads 7` |
| --- | ---: | ---: |
| `particles_free_4k` | 60 (15) | 24 (6) |
| `particles_box_floor_4k` | 181 (44) | 42 (10) |
| `particles_sdf_far_4k` | 88 (22) | 31 (7) |
| `particles_sdf_terrain_4k` | 848 (207) | 141 (34) |
| `particles_sdf_terrain_16k` | 3,374 (206) | 535 (33) |
| `particles_sdf_pool_4k` | 769 (188) | 131 (32) |

An SDF boundary costs about 200 ns per particle on one thread, linear in the count, and scales with the workers. While the particles fall (the minimum, about 85 ns) the cost is one `sample` at each end; a particle at rest adds one `gradient`. The first build swept every contact with `sweep_circle` and spent 1.3 us per resting particle (5.3 ms for the 4k terrain row); projecting short steps into the surface along the gradient, and reading the starts only for particles the end distance cannot clear, brought it to the numbers above.

## Fluids (LQ-27)

`bench/physics/bench_fluid_2d.cpp`, in the `--2d` suite. Each row calls `run_fluid_2d` directly (no `install_fluid_2d`) and times it alone (gather included) for 300 fixed ticks of 1/60 s from the release: a dam break. A square column of particles at rest spacing (`radius` 0.05, every other parameter default: 2 substeps, 4 iterations) stands against the left wall of an open-top `SdfCollider2D` tank (0.1-unit cells, 0.4-unit band) 2.5 columns wide, gravity -10. The "Bodies" column is the particle count. Stability: no particle lost or non-finite, none closer than half a radius to the walls.

| Scenario | What it measures |
| --- | --- |
| `fluid_dam_4k` | a 64 x 64 column (4,096 particles, 6.4 units) collapsing across a 16-unit tank |
| `fluid_dam_16k` | a 128 x 128 column (16,384 particles) in a 32-unit tank |
| `fluid_rest_4k` | `fluid_dam_4k` after 600 untimed ticks: the pool has spread and come to rest (LQ-59) |

Windows, MSVC, RelWithDebInfo, 12 hardware threads, mean microseconds per tick, ns per particle in brackets:

| Scenario | `--threads 0` | default (`--threads 11`) |
| --- | ---: | ---: |
| `fluid_dam_4k` | 18,994 (4,637) | 3,161 (772) |
| `fluid_dam_16k` | 78,954 (4,819) | 10,372 (633) |

The cost is linear in the count and scales about 6 to 7.5 times on 11 workers (the hash fill is serial). Per substep, at 4k on one thread: the neighbour search is about 1.3 ms, each solver iteration about 1.4 ms (two passes over about 24 neighbours) plus about 0.5 ms of SDF projection, so 2 x 4 iterations dominate; halving `iterations` or `substeps` is the main knob. Keeping each neighbour's kernel gradient and weight from the lambda pass for the delta pass (instead of recomputing the square root and division) brought the 4k serial row from 28.5 ms to 19 ms. A 60 Hz game on one core fits about 3k particles in 15 ms; on a typical worker pool, 16k fits in about 10 ms.

### Swept boundaries (LQ-59)

Particles that move more than half a radius in an iteration are swept against the boundaries ([ECS](../modules/ECS.md#2d-fluids)). Same settings as above, best of two runs each, before and after:

| Scenario | `--threads 0` before | after | default threads before | after |
| --- | ---: | ---: | ---: | ---: |
| `fluid_dam_4k` | 19,255 | 20,056 (+4%) | 3,210 | 3,370 (+5%) |
| `fluid_dam_16k` | 79,970 | 85,864 (+7%) | 10,765 | 11,499 (+7%) |
| `fluid_rest_4k` | 20,322 | 20,378 (+0.3%) | 3,440 | 3,518 (+2%) |

A resting pool pays one length test per particle per iteration. The collapse pays for the particles that fall faster than half a radius per substep, swept once (the first iteration's move holds the predicted step; later iterations move by the solver delta, which rarely passes the threshold). Sweeping every fast particle from the substep start in every iteration instead cost +15 and +28 percent on the two dam rows.

## Deterministic simulation (LQ-63)

The switch from libm `sin`, `cos` and `atan2` to the engine's deterministic functions ([Principles](../architecture/Principles.md#deterministic-simulation)), measured against the commit before it: the whole suite, `--all --threads 0 --skip-large`, best of two runs each, Windows, MSVC, RelWithDebInfo. MSVC already compiled with `/fp:precise` and no contraction, so on this target the change is the trigonometry alone. Most rows move within the run-to-run noise (about 5 percent). Rows dominated by contacts on rotated shapes cost a little more: `tumbler` 21,724 to 22,612 us (+4 percent), `static_contact_2k` 998 to 1,045 (+5), `static_contact_2k_compound` 965 to 1,025 (+6), `3d_large_island` 2,198 to 2,300 (+5). The fluid gets slightly cheaper (`fluid_dam_4k` 20,087 to 19,747), since collider frames now come from the matrix axis without an angle. The cost per platform (clang, Emscripten, arm64) is a separate backlog ticket.

## See also

- [ECS](../modules/ECS.md#physics), the static collider cache and `mark_static_colliders_dirty_2d`
- [ECS](../modules/ECS.md#compound-colliders), `CompoundCollider2D`
- [ECS](../modules/ECS.md#sdf-colliders), `SdfCollider2D`
- [UI Bench](UI%20Bench.md)
