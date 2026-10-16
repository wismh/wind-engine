#pragma once

// docs/tech/modules/Render.md (Lights)

#include <engine/ecs/world.h>
#include <engine/render/scene_lights.h>

namespace engine::render {

// The default rig of a world without light entities: one `kDefaultLightDirection` (normalized) light of
// `kDefaultLightColor` and `kDefaultAmbient`.
[[nodiscard]] SceneLights default_scene_lights();

// Every `DirectionalLight` and `AmbientLight` of `world`, as `run_render` puts them into `CmdSetView`:
// - directional lights in entity index order, the first `kMaxDirectionalLights` of them (more log one warning per
//   process and are ignored); each direction is the normalized -Z column of `ecs::world_matrix`, or -Z when that
//   column is zero;
// - ambient is the sum over all ambient lights;
// - a world with neither kind returns `default_scene_lights()`. A world with only ambient lights gets no directional
//   light, and one with only directional lights gets no ambient.
[[nodiscard]] SceneLights collect_scene_lights(ecs::World& world);

}
