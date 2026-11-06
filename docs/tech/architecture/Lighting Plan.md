# Forward lighting plan

Decision record. Slices 1–4 are built; the running model is [Render](../modules/Render.md#lights). Deferred stays out. A 2D game that never adds a light component, and a sprite that stays on `material_unlit`, still draws unlit.

## Goal

One forward model for 2D and 3D. A sun, a point, and a flashlight are data on entities. The frame gathers them into one view list. Every lit shader evaluates that list with the same function. Unlit sprites, tilemaps, particles, and UI do not read it.

The list is the durable seam. A later per-draw index list, a shadow index, or a normal map reads the same records. None of those need a new light type or a second collector.

## What this is not

- Not a light class hierarchy, a virtual `ILight`, or a lighting service. Components stay plain data. `run_render` still copies a `SceneLights` into `CmdSetView`. Backends upload that and do not walk the `World`.
- Not a `Light2D` beside the 3D lights. A point in the world lights a mesh and a lit sprite. The sprite opts in with a material.
- Not deferred, clustered, or tiled. The cap below is the whole camera list.
- Not PBR, not shadow maps, not 2D occluders. Those land later as fields and passes on this list, not as a replacement.
- Not a tonemap inside the lit shader. Values stay linear. `PostStack` already tonemaps when `hdr` is set.

## Shape

Four steps. Each has one job. The first three are CPU functions with tests. The last is a shader.

1. **Author.** `DirectionalLight` and `AmbientLight` stay. Add `PointLight` (`color`, `intensity`, `range`) and `SpotLight` (`color`, `intensity`, `range`, `inner_angle`, `outer_angle`). Position is the entity's world translation. Direction is the normalized −Z column of `ecs::world_matrix`, the same rule the sun already uses. A flashlight is a `SpotLight`, not a directional. One component type per header, serialized as `point_light` and `spot_light` next to the existing entries.
2. **Gather.** One function reads those components and returns light records in world space. No cap, no frustum, no GPU layout. Ambient is not a record: it is the sum of `color * intensity`, as today.
3. **Select.** A pure function takes the records, the view frustum, and the camera position, and returns the `SceneLights` of one view. `collect_scene_lights` is that gather plus this select. `default_scene_lights` is the same struct, used when the world has no light component of any kind.
4. **Shade.** One function, shared by the lit shaders, loops the view list. `lit`, `lit_array`, and `skin_lit` call it with Lambert on. `sprite_lit` calls it with Lambert off until a normal map exists.

`CmdDrawMesh` does not carry lights. The view does. A later per-object list is an index range into this same array, produced by a second select over mesh bounds. Draws stay free of ECS types.

## The view list

`SceneLights` stops being parallel `directions[]` and `colors[]` arrays. It becomes one array of records plus the ambient sum:

| Field | Meaning |
| --- | --- |
| `kind` | Directional, point, or spot |
| `color` | `color * intensity` |
| `direction` | Unit vector the light travels along. Directional and spot. Unused for a point |
| `position` | World position. Point and spot |
| `range` | World units. Unused for a directional |
| `spot_scale`, `spot_bias` | Cone terms, packed on the CPU so the shader does not call `cos` |

`kMaxForwardLights` is 16. `kMaxDirectionalLights` stays 4 and is a reserved prefix of that 16, not a second array. Select fills directionals first (lowest entity index, the cut already shipped), then point and spot whose sphere intersects the view frustum, nearest to the camera, entity index as the tie-break. A row of torches cannot drop the sun. More than the cap logs once per process and the rest are ignored. A world with any light component gets exactly its lights: a lone point has no sun and no ambient. A world with none gets today's default rig, packed as one directional record plus ambient `0.2`.

The shader loops to 16 and breaks at `count`. An unknown kind adds nothing.

Attenuation for point and spot is windowed so the edge is zero and the core does not blow up:

```
window = saturate(1 - (distance / range)^2)^2
atten  = window / (distance^2 + 1)
```

A directional has attenuation 1. A spot multiplies by `saturate(dot(-L, direction) * spot_scale + spot_bias)`, which is 1 inside `inner_angle` and 0 outside `outer_angle`. `inner_angle` greater than or equal to `outer_angle` is a hard cone at `outer_angle`.

Lambert is `max(dot(N, -L), 0)` for meshes. For a sprite it is 1. A quad normal is +Z, so a point in the sprite's XY plane is perpendicular to that normal and Lambert would zero the sprite. The 2D blob is distance and cone, not NdotL. A normal map, when it exists, turns Lambert back on for that sprite.

## Where the code lives

Gather and select stay in `src/render/lights.cpp`. Packing a component into a record happens in that one file, nowhere else. `pack_forward_lights` runs once per view on the CPU. Backends upload those vec4 arrays with the other view uniforms on each draw: Vulkan reflects a single UBO, and that block also holds the per-draw `uModel`, so lights are not a second block. A shader that does not declare the uniforms stays unlit. OpenGL and Vulkan both write the same field names.

The shade function is not copied into every CDATA block. `parse_shader` expands one include in the slang text and one in the glsl text before compile, after the XML is read, because the snippet contains `<`. Two snippet files, `.forward_light.slang` and `.forward_light.glsl`, hold the function and the uniforms. The leading dot keeps them out of the asset scan. Builtin lit shaders include them. This is not a general preprocessor. Game shaders that want the same lights include the same file, or they declare the same uniforms and call the same math.

`material_sprite_lit` is `sprite_lit` with alpha blend, no depth, no cull. `builtin::material_unlit` stays the sprite default. Tilemaps stay on the unlit cache. Particles and UI are not lit. `render_world_viewport` already receives a `CmdSetView`, so it lights the same way without a second path.

Specular and a normal map are material data, not light data: a `.mat` gloss, and texture slot 1 sampled only by the shader that declares it. Slot 0 stays albedo. The 1×1 white texture is not a flat normal.

## Slices

Each slice ships on its own. Behavior of a scene with only the old components stays the same after slice 1.

1. **The list.** Replace the parallel arrays, upload the block once per view, move the existing Lambert loop into the include. Directional and ambient only. `lights_test` covers pack, the reserved prefix, and the default rig.
2. **Point and spot.** Components, serializer keys, frustum select, cap, attenuation, cone. `lit`, `lit_array`, and `skin_lit` go through the include. Vertex stage passes world position.
3. **Sprites.** `sprite_lit` and `material_sprite_lit`. Lambert off. A sprite that keeps `material_unlit` is unchanged.
4. **Surface.** Gloss on the material, Blinn-Phong in the same function, normal-map slot. The sprite turns Lambert on only when that slot is set.

## Later, not these slices

Per-draw indices into the same 16. One directional shadow map, addressed by a shadow index on the record. 2D occluders as a visibility mask for a spot or a point. PBR as a different shade function over the same list. Raising 16 is a constant and a shader define, changed together, not an API change.
