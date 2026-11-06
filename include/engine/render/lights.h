#pragma once

// docs/tech/modules/Render.md (Lights)

#include <engine/ecs/world.h>
#include <engine/render/frustum.h>
#include <engine/render/scene_lights.h>

#include <glm/vec3.hpp>

#include <span>

namespace engine::render {

// The default rig of a world without light components: one directional `kDefaultLightDirection` (normalized) of
// `kDefaultLightColor`, and `kDefaultAmbient`.
[[nodiscard]] SceneLights default_scene_lights();

// Every light component of `world`, packed into one view list:
// - no light component of any kind returns `default_scene_lights()`;
// - ambient is the sum of `color * intensity` over every `AmbientLight`;
// - directional lights come first, at most `kMaxDirectionalLights`, lowest entity index, each aimed along the
//   normalized -Z column of `ecs::world_matrix` (or -Z when that column is zero);
// - point and spot lights follow, those whose range sphere meets `frustums` (an empty span keeps them all), nearest
//   to `camera_position`, entity index breaks a tie, filling `kMaxForwardLights`;
// - past either cap, one warning per process, and the rest are ignored.
// A world with only ambient has no directional light. A world with only a point has no sun and no ambient.
// `run_render` passes the frame's frustums and the camera position. The one-argument overload culls nothing and
// measures distance from the origin.
[[nodiscard]] SceneLights collect_scene_lights(ecs::World& world);
[[nodiscard]] SceneLights collect_scene_lights(
        ecs::World& world, std::span<const Frustum> frustums, glm::vec3 camera_position);

}
