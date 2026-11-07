# Decals

A `Decal` plus a `Transform` projects a texture through a unit cube onto the scene that was drawn before it.

## Component

`include/engine/render/decal.h`. The cube is `builtin::mesh_cube` (local extent [-0.5, 0.5]) unless `mesh` is set. The raster hull is that mesh. `builtin::shader_decal` still clips to the unit cube in the mesh's local space, so a larger mesh only grows the pixels that run the shader. Scale and rotation come from the world matrix.

`material` null and `texture` set builds `Material(builtin::shader_decal, texture, white, alpha, no depth test, no depth write, cull front)` and caches it on `DecalMaterialCache`, keyed by the texture pointer. That cache is not the sprite one: a sprite and a decal can share a texture and keep different state. No material and no texture is fatal. An explicit `material` is used as-is.

`angle_cutoff` is a cosine. 0 disables the fade. Above 0, alpha fades from that cosine to `angle_cutoff + normal_fade` as the surface normal leaves the decal's local +Z. `uv_scale` and `uv_offset` apply after the local XY of the hit is mapped from [-0.5, 0.5] to [0, 1]. `layer` and `order_in_layer` sort inside the decal queue. `frustum_cull` defaults to true.

`set_param` / `clear_overrides` are the same per-draw `MaterialOverride` as a sprite. The override is not serialized.

## Draw

`run_render` emits `CmdDrawDecal` in `DrawQueue::Decal`: after every opaque draw, before transparent ones. Any decal in the world sets `PostChain::sample_depth`, so the scene is an offscreen target with a depth texture. A chain that was otherwise empty copies that color to the window (`shader_post_grade` on Vulkan, a color blit on OpenGL).

The backend copies the scene depth into a texture that is not attached to the framebuffer, then the shader reconstructs the world position from it. Sampling the attached depth is a feedback loop, so the copy happens once, on the first decal, after the opaque draws. Later decals in the same view read that snapshot. `uHasDepth` is 0 when there is no scene depth, and the builtin shader discards.

The projection axis is the world +Z of `model`. OpenGL clip z is [-1, 1]. Vulkan clip z is [0, 1], and the negative viewport flips Y, so `uNdcScale` and `uNdcBias` differ per backend. `uInvViewProj` matches the matrix that backend wrote as `uProjection`.
