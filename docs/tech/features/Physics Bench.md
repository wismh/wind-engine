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

## See also

- [ECS](../modules/ECS.md#physics), the static collider cache and `mark_static_colliders_dirty_2d`
- [ECS](../modules/ECS.md#compound-colliders), `CompoundCollider2D`
- [UI Bench](UI%20Bench.md)
